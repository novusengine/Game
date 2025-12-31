#include "DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

AutoCVar_Int CVAR_DebugRendererNumGPUVertices(CVarCategory::Client | CVarCategory::Rendering, "debugRendererNumGPUVertices", "number of GPU vertices to allocate for", 32000000);
AutoCVar_ShowFlag CVAR_DebugRendererAlwaysOnTop(CVarCategory::Client | CVarCategory::Rendering, "debugRendererAlwaysOnTop", "always show debug renderer on top", ShowFlag::DISABLED);

DebugRenderer::DebugRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
    : _gameRenderer(gameRenderer)
    , _debugDescriptorSet(Renderer::DescriptorSetSlot::DEBUG)
    , _draw2DDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _draw2DIndirectDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _draw3DDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _draw3DIndirectDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _drawSolid2DDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _drawSolid3DDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    _renderer = renderer;

    CreatePermanentResources();
}

void DebugRenderer::CreatePermanentResources()
{
    CreatePipelines();
    InitDescriptorSets();

    _debugVertices2D.SetDebugName("DebugVertices2D");
    _debugVertices2D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
    _debugVertices2D.SyncToGPU(_renderer);
    _draw2DDescriptorSet.Bind("_vertices", _debugVertices2D.GetBuffer());

    _debugVertices3D.SetDebugName("DebugVertices3D");
    _debugVertices3D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
    _debugVertices3D.SyncToGPU(_renderer);
    _draw3DDescriptorSet.Bind("_vertices", _debugVertices3D.GetBuffer());

    _debugVerticesSolid2D.SetDebugName("DebugVerticesSolid2D");
    _debugVerticesSolid2D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
    _debugVerticesSolid2D.SyncToGPU(_renderer);
    _drawSolid2DDescriptorSet.Bind("_vertices", _debugVerticesSolid2D.GetBuffer());

    _debugVerticesSolid3D.SetDebugName("DebugVerticesSolid3D");
    _debugVerticesSolid3D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
    _debugVerticesSolid3D.SyncToGPU(_renderer);
    _drawSolid3DDescriptorSet.Bind("_vertices", _debugVerticesSolid3D.GetBuffer());

    // Create indirect buffers for GPU-side debugging
    u32 numGPUVertices = CVAR_DebugRendererNumGPUVertices.Get();
    {
        Renderer::BufferDesc desc;
        desc.name = "DebugVertices2D";
        desc.size = sizeof(DebugVertex2D) * numGPUVertices;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

        _gpuDebugVertices2D = _renderer->CreateBuffer(_gpuDebugVertices2D, desc);
        _draw2DIndirectDescriptorSet.Bind("_vertices", _gpuDebugVertices2D);
        _debugDescriptorSet.Bind("_debugVertices2D", _gpuDebugVertices2D);
    }

    {
        Renderer::BufferDesc desc;
        desc.name = "DebugVertices3D";
        desc.size = sizeof(DebugVertex3D) * numGPUVertices;
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

        _gpuDebugVertices3D = _renderer->CreateBuffer(_gpuDebugVertices3D, desc);
        _draw3DIndirectDescriptorSet.Bind("_vertices", _gpuDebugVertices3D);
        _debugDescriptorSet.Bind("_debugVertices3D", _gpuDebugVertices3D);
    }

    // Create indirect argument buffers for GPU-side debugging
    {
        Renderer::BufferDesc desc;
        desc.name = "DebugVertices2DArgument";
        desc.size = sizeof(Renderer::IndirectDraw);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;

        _gpuDebugVertices2DArgumentBuffer = _renderer->CreateAndFillBuffer(_gpuDebugVertices2DArgumentBuffer, desc, [](void* mappedMemory, size_t size)
        {
            Renderer::IndirectDraw* indirectDraw = static_cast<Renderer::IndirectDraw*>(mappedMemory);
            indirectDraw->instanceCount = 1;
        });
        _debugDescriptorSet.Bind("_debugVertices2DCount", _gpuDebugVertices2DArgumentBuffer);
    }

    {
        Renderer::BufferDesc desc;
        desc.name = "DebugVertices3DArgument";
        desc.size = sizeof(Renderer::IndirectDraw);
        desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER;

        _gpuDebugVertices3DArgumentBuffer = _renderer->CreateAndFillBuffer(_gpuDebugVertices3DArgumentBuffer, desc, [](void* mappedMemory, size_t size)
        {
            Renderer::IndirectDraw* indirectDraw = static_cast<Renderer::IndirectDraw*>(mappedMemory);
            indirectDraw->instanceCount = 1;
        });
        _debugDescriptorSet.Bind("_debugVertices3DCount", _gpuDebugVertices3DArgumentBuffer);
    }
}

void DebugRenderer::CreatePipelines()
{
    // Create pipelines
    Renderer::GraphicsPipelineDesc pipelineDesc;
    // 2D Solid and Wireframe
    {
        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;

        // Render targets.
        pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();

        // Shader
        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Debug/Debug2D.vs"_h, "Debug/Debug2D.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Debug/Debug2D.ps"_h, "Debug/Debug2D.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Lines;
        _debugLine2DPipeline = _renderer->CreatePipeline(pipelineDesc);

        pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Triangles;
        _debugSolid2DPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // 3D Solid
    {
        // Shader
        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Debug/DebugSolid3D.vs"_h, "Debug/DebugSolid3D.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Debug/DebugSolid3D.ps"_h, "Debug/DebugSolid3D.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        // Depth state
        pipelineDesc.states.depthStencilState.depthEnable = true;
        pipelineDesc.states.depthStencilState.depthWriteEnable = true;
        pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

        pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();

        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Triangles;
        _debugSolid3DPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // 3D Wireframe
    {
        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Debug/Debug3D.vs"_h, "Debug/Debug3D.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Debug/Debug3D.ps"_h, "Debug/Debug3D.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        pipelineDesc.states.depthStencilState.depthWriteEnable = false;

        pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Lines;
        _debugLine3DPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
}

void DebugRenderer::InitDescriptorSets()
{
    // Line 2d
    _draw2DDescriptorSet.RegisterPipeline(_renderer, _debugLine2DPipeline);
    _draw2DDescriptorSet.Init(_renderer);

    _draw2DIndirectDescriptorSet.RegisterPipeline(_renderer, _debugLine2DPipeline);
    _draw2DIndirectDescriptorSet.Init(_renderer);

    // Line 3d
    _draw3DDescriptorSet.RegisterPipeline(_renderer, _debugLine3DPipeline);
    _draw3DDescriptorSet.Init(_renderer);

    _draw3DIndirectDescriptorSet.RegisterPipeline(_renderer, _debugLine3DPipeline);
    _draw3DIndirectDescriptorSet.Init(_renderer);

    // Solid 2d
    _drawSolid2DDescriptorSet.RegisterPipeline(_renderer, _debugSolid2DPipeline);
    _drawSolid2DDescriptorSet.Init(_renderer);

    // Solid 3d
    _drawSolid3DDescriptorSet.RegisterPipeline(_renderer, _debugSolid3DPipeline);
    _drawSolid3DDescriptorSet.Init(_renderer);

    // Material pass descriptor set
    _debugDescriptorSet.RegisterPipeline(_renderer, _debugLine2DPipeline);
    _debugDescriptorSet.Init(_renderer);
}

void DebugRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    // Draw world axises
    DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(100.0f, 0.0f, 0.0f), Color::Red);
    DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 100.0f, 0.0f), Color::Green);
    DrawLine3D(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 100.0f), Color::Blue);
}

void DebugRenderer::AddStartFramePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::BufferMutableResource gpuDebugVertices2DArgumentBuffer;
        Renderer::BufferMutableResource gpuDebugVertices3DArgumentBuffer;
    };

    renderGraph->AddPass<Data>("DebugRenderReset",
        [this](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.gpuDebugVertices2DArgumentBuffer = builder.Write(_gpuDebugVertices2DArgumentBuffer, BufferUsage::TRANSFER);
            data.gpuDebugVertices3DArgumentBuffer = builder.Write(_gpuDebugVertices3DArgumentBuffer, BufferUsage::TRANSFER);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, DebugRenderReset);

            commandList.FillBuffer(data.gpuDebugVertices2DArgumentBuffer, 0, 4, 0); // Reset vertexCount to 0
            commandList.FillBuffer(data.gpuDebugVertices3DArgumentBuffer, 0, 4, 0); // Reset vertexCount to 0
        });
}

void DebugRenderer::Add2DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    // Sync to GPU
    if (_debugVertices2D.SyncToGPU(_renderer))
    {
        _draw2DDescriptorSet.Bind("_vertices", _debugVertices2D.GetBuffer());
    }
    if (_debugVerticesSolid2D.SyncToGPU(_renderer))
    {
        _drawSolid2DDescriptorSet.Bind("_vertices", _debugVerticesSolid2D.GetBuffer());
    }

    struct Data
    {
        Renderer::ImageMutableResource color;

        Renderer::BufferResource gpuDebugVertices2D;
        Renderer::BufferResource gpuDebugVertices2DArgumentBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource draw2DSet;
        Renderer::DescriptorSetResource draw2DIndirectSet;
        Renderer::DescriptorSetResource drawSolid2DSet;
    };
    renderGraph->AddPass<Data>("DebugRender2D",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.color = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            data.gpuDebugVertices2D = builder.Read(_gpuDebugVertices2D, BufferUsage::GRAPHICS);
            data.gpuDebugVertices2DArgumentBuffer = builder.Read(_gpuDebugVertices2DArgumentBuffer, BufferUsage::GRAPHICS);
            builder.Read(_debugVertices2D.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_debugVerticesSolid2D.GetBuffer(), BufferUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.draw2DSet = builder.Use(_draw2DDescriptorSet);
            data.draw2DIndirectSet = builder.Use(_draw2DIndirectDescriptorSet);
            data.drawSolid2DSet = builder.Use(_drawSolid2DDescriptorSet);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender2D);

            Renderer::RenderPassDesc renderPassDesc;
            graphResources.InitializeRenderPassDesc(renderPassDesc);
            renderPassDesc.renderTargets[0] = data.color;
            commandList.BeginRenderPass(renderPassDesc);

            // Solid
            {
                Renderer::GraphicsPipelineID pipeline = _debugSolid2DPipeline;
                // CPU side debug rendering
                {
                    commandList.BeginPipeline(pipeline);

                    //commandList.BindDescriptorSet2(data.globalSet, frameIndex); // TODO: Enable this with validation layers and find a way to print a better warning that this shader doesn't need this descriptorset
                    commandList.BindDescriptorSet(data.drawSolid2DSet, frameIndex);

                    // Draw
                    commandList.Draw(static_cast<u32>(_debugVerticesSolid2D.Count()), 1, 0, 0);

                    commandList.EndPipeline(pipeline);
                }
                _debugVerticesSolid2D.Clear();
            }

            // Wireframe
            {
                Renderer::GraphicsPipelineID pipeline = _debugLine2DPipeline;
                // CPU side debug rendering
                {
                    commandList.BeginPipeline(pipeline);

                    //commandList.BindDescriptorSet2(data.globalSet, frameIndex); // TODO: Enable this with validation layers and find a way to print a better warning that this shader doesn't need this descriptorset
                    commandList.BindDescriptorSet(data.draw2DSet, frameIndex);

                    // Draw
                    commandList.Draw(static_cast<u32>(_debugVertices2D.Count()), 1, 0, 0);

                    commandList.EndPipeline(pipeline);
                }
                _debugVertices2D.Clear();

                // GPU side debug rendering
                {
                    commandList.BeginPipeline(pipeline);

                    //commandList.BindDescriptorSet2(data.globalSet, frameIndex); // TODO: Enable this with validation layers and find a way to print a better warning that this shader doesn't need this descriptorset
                    commandList.BindDescriptorSet(data.draw2DIndirectSet, frameIndex);

                    // Draw
                    commandList.DrawIndirect(data.gpuDebugVertices2DArgumentBuffer, 0, 1);

                    commandList.EndPipeline(pipeline);
                }
            }

            commandList.EndRenderPass(renderPassDesc);
        });
}

void DebugRenderer::Add3DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    // Sync to GPU
    if (_debugVertices3D.SyncToGPU(_renderer))
    {
        _draw3DDescriptorSet.Bind("_vertices", _debugVertices3D.GetBuffer());
    }
    if (_debugVerticesSolid3D.SyncToGPU(_renderer))
    {
        _drawSolid3DDescriptorSet.Bind("_vertices", _debugVerticesSolid3D.GetBuffer());
    }

    struct Data
    {
        Renderer::ImageMutableResource color;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferResource gpuDebugVertices3D;
        Renderer::BufferResource gpuDebugVertices3DArgumentBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource draw3DSet;
        Renderer::DescriptorSetResource draw3DIndirectSet;
        Renderer::DescriptorSetResource drawSolid3DSet;
    };
    renderGraph->AddPass<Data>("DebugRender3D",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.color = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            if (CVAR_DebugRendererAlwaysOnTop.Get() == ShowFlag::ENABLED)
            {
                data.depth = builder.Write(resources.debugRendererDepth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
            }
            else
            {
                data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            }

            data.gpuDebugVertices3D = builder.Read(_gpuDebugVertices3D, BufferUsage::GRAPHICS);
            data.gpuDebugVertices3DArgumentBuffer = builder.Read(_gpuDebugVertices3DArgumentBuffer, BufferUsage::GRAPHICS);
            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_debugVertices3D.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_debugVerticesSolid3D.GetBuffer(), BufferUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.draw3DSet = builder.Use(_draw3DDescriptorSet);
            data.draw3DIndirectSet = builder.Use(_draw3DIndirectDescriptorSet);
            data.drawSolid3DSet = builder.Use(_drawSolid3DDescriptorSet);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender3D);

            Renderer::RenderPassDesc renderPassDesc;
            graphResources.InitializeRenderPassDesc(renderPassDesc);

            // Render targets
            renderPassDesc.renderTargets[0] = data.color;
            renderPassDesc.depthStencil = data.depth;
            commandList.BeginRenderPass(renderPassDesc);

            // Solid
            {
                Renderer::GraphicsPipelineID pipeline = _debugSolid3DPipeline;

                // CPU side debug rendering
                {
                    commandList.BeginPipeline(pipeline);

                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.drawSolid3DSet, frameIndex);

                    // Draw
                    commandList.Draw(static_cast<u32>(_debugVerticesSolid3D.Count()), 1, 0, 0);

                    commandList.EndPipeline(pipeline);
                }
                _debugVerticesSolid3D.Clear();
            }

            // Wireframe
            {
                Renderer::GraphicsPipelineID pipeline = _debugLine3DPipeline;

                // CPU side debug rendering
                {
                    commandList.BeginPipeline(pipeline);

                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.draw3DSet, frameIndex);

                    // Draw
                    commandList.Draw(static_cast<u32>(_debugVertices3D.Count()), 1, 0, 0);

                    commandList.EndPipeline(pipeline);
                }
                _debugVertices3D.Clear();

                // GPU side debug rendering
                {
                    commandList.BeginPipeline(pipeline);

                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.draw3DIndirectSet, frameIndex);

                    // Draw
                    commandList.DrawIndirect(data.gpuDebugVertices3DArgumentBuffer, 0, 1);

                    commandList.EndPipeline(pipeline);
                }
            }

            commandList.EndRenderPass(renderPassDesc);
        });
}

void DebugRenderer::DrawLine2D(const glm::vec2& from, const glm::vec2& to, Color color)
{
    u32 colorInt = color.ToABGR32();

    u32 index = _debugVertices2D.AddCount(2);
    _debugVertices2D[index + 0] = { from, colorInt };
    _debugVertices2D[index + 1] = { to, colorInt };
}

void DebugRenderer::DrawLine3D(const glm::vec3& from, const glm::vec3& to, Color color)
{
    u32 colorInt = color.ToABGR32();

    u32 index = _debugVertices3D.AddCount(2);
    _debugVertices3D[index + 0] = { from, colorInt };
    _debugVertices3D[index + 1] = { to, colorInt };
}

void DebugRenderer::DrawBox2D(const vec2& center, const vec2& extents, Color color)
{
    const vec2& renderSize = _renderer->GetRenderSize();

    vec2 v0 = (center - extents) / renderSize;
    vec2 v1 = (center + extents) / renderSize;
    u32 colorInt = color.ToABGR32();

    u32 index = _debugVertices2D.AddCount(8);
    _debugVertices2D[index + 0] = { { v0.x, v0.y }, colorInt };
    _debugVertices2D[index + 1] = { { v1.x, v0.y }, colorInt };
    _debugVertices2D[index + 2] = { { v1.x, v0.y }, colorInt };
    _debugVertices2D[index + 3] = { { v1.x, v1.y }, colorInt };
    _debugVertices2D[index + 4] = { { v1.x, v1.y }, colorInt };
    _debugVertices2D[index + 5] = { { v0.x, v1.y }, colorInt };
    _debugVertices2D[index + 6] = { { v0.x, v1.y }, colorInt };
    _debugVertices2D[index + 7] = { { v0.x, v0.y }, colorInt };
}

void DebugRenderer::DrawAABB3D(const vec3& center, const vec3& extents, Color color)
{
    vec3 v0 = center - extents;
    vec3 v1 = center + extents;

    u32 colorInt = color.ToABGR32();

    u32 index = _debugVertices3D.AddCount(24);

    // Bottom
    _debugVertices3D[index +  0] = { { v0.x, v0.y, v0.z }, colorInt };
    _debugVertices3D[index +  1] = { { v1.x, v0.y, v0.z }, colorInt };
    _debugVertices3D[index +  2] = { { v1.x, v0.y, v0.z }, colorInt };
    _debugVertices3D[index +  3] = { { v1.x, v0.y, v1.z }, colorInt };
    _debugVertices3D[index +  4] = { { v1.x, v0.y, v1.z }, colorInt };
    _debugVertices3D[index +  5] = { { v0.x, v0.y, v1.z }, colorInt };
    _debugVertices3D[index +  6] = { { v0.x, v0.y, v1.z }, colorInt };
    _debugVertices3D[index +  7] = { { v0.x, v0.y, v0.z }, colorInt };

    // Top
    _debugVertices3D[index +  8] = { { v0.x, v1.y, v0.z }, colorInt };
    _debugVertices3D[index +  9] = { { v1.x, v1.y, v0.z }, colorInt };
    _debugVertices3D[index + 10] = { { v1.x, v1.y, v0.z }, colorInt };
    _debugVertices3D[index + 11] = { { v1.x, v1.y, v1.z }, colorInt };
    _debugVertices3D[index + 12] = { { v1.x, v1.y, v1.z }, colorInt };
    _debugVertices3D[index + 13] = { { v0.x, v1.y, v1.z }, colorInt };
    _debugVertices3D[index + 14] = { { v0.x, v1.y, v1.z }, colorInt };
    _debugVertices3D[index + 15] = { { v0.x, v1.y, v0.z }, colorInt };

    // Vertical edges
    _debugVertices3D[index + 16] = { { v0.x, v0.y, v0.z }, colorInt };
    _debugVertices3D[index + 17] = { { v0.x, v1.y, v0.z }, colorInt };
    _debugVertices3D[index + 18] = { { v1.x, v0.y, v0.z }, colorInt };
    _debugVertices3D[index + 19] = { { v1.x, v1.y, v0.z }, colorInt };
    _debugVertices3D[index + 20] = { { v0.x, v0.y, v1.z }, colorInt };
    _debugVertices3D[index + 21] = { { v0.x, v1.y, v1.z }, colorInt };
    _debugVertices3D[index + 22] = { { v1.x, v0.y, v1.z }, colorInt };
    _debugVertices3D[index + 23] = { { v1.x, v1.y, v1.z }, colorInt };
}

void DebugRenderer::DrawOBB3D(const vec3& center, const vec3& extents, const quat& rotation, Color color)
{
    u32 colorInt = color.ToABGR32();
    vec3 corners[8] = {
        center + rotation * glm::vec3(-extents.x, -extents.y, -extents.z),
        center + rotation * glm::vec3( extents.x, -extents.y, -extents.z),
        center + rotation * glm::vec3( extents.x, -extents.y,  extents.z),
        center + rotation * glm::vec3(-extents.x, -extents.y,  extents.z),
        center + rotation * glm::vec3(-extents.x,  extents.y, -extents.z),
        center + rotation * glm::vec3( extents.x,  extents.y, -extents.z),
        center + rotation * glm::vec3( extents.x,  extents.y,  extents.z),
        center + rotation * glm::vec3(-extents.x,  extents.y,  extents.z)
    };

    u32 index = _debugVertices3D.AddCount(24);

    // Bottom
    _debugVertices3D[index + 0] = { corners[0], colorInt };
    _debugVertices3D[index + 1] = { corners[1], colorInt };
    _debugVertices3D[index + 2] = { corners[1], colorInt };
    _debugVertices3D[index + 3] = { corners[2], colorInt };
    _debugVertices3D[index + 4] = { corners[2], colorInt };
    _debugVertices3D[index + 5] = { corners[3], colorInt };
    _debugVertices3D[index + 6] = { corners[3], colorInt };
    _debugVertices3D[index + 7] = { corners[0], colorInt };

    // Top
    _debugVertices3D[index + 8] = { corners[4], colorInt };
    _debugVertices3D[index + 9] = { corners[5], colorInt };
    _debugVertices3D[index + 10] = { corners[5], colorInt };
    _debugVertices3D[index + 11] = { corners[6], colorInt };
    _debugVertices3D[index + 12] = { corners[6], colorInt };
    _debugVertices3D[index + 13] = { corners[7], colorInt };
    _debugVertices3D[index + 14] = { corners[7], colorInt };
    _debugVertices3D[index + 15] = { corners[4], colorInt };

    // Vertical edges
    _debugVertices3D[index + 16] = { corners[0], colorInt };
    _debugVertices3D[index + 17] = { corners[4], colorInt };
    _debugVertices3D[index + 18] = { corners[1], colorInt };
    _debugVertices3D[index + 19] = { corners[5], colorInt };
    _debugVertices3D[index + 20] = { corners[2], colorInt };
    _debugVertices3D[index + 21] = { corners[6], colorInt };
    _debugVertices3D[index + 22] = { corners[3], colorInt };
    _debugVertices3D[index + 23] = { corners[7], colorInt };
}

void DebugRenderer::DrawTriangle2D(const vec2& v0, const vec2& v1, const vec2& v2, Color color)
{
    u32 colorInt = color.ToABGR32();

    u32 index = _debugVertices2D.AddCount(6);

    _debugVertices2D[index + 0] = { v0, colorInt };
    _debugVertices2D[index + 1] = { v1, colorInt };

    _debugVertices2D[index + 2] = { v1, colorInt };
    _debugVertices2D[index + 3] = { v2, colorInt };

    _debugVertices2D[index + 4] = { v2, colorInt };
    _debugVertices2D[index + 5] = { v0, colorInt };
}

void DebugRenderer::DrawTriangle3D(const vec3& v0, const vec3& v1, const vec3& v2, Color color)
{
    u32 colorInt = color.ToABGR32();

    u32 index = _debugVertices3D.AddCount(6);

    _debugVertices3D[index + 0] = { v0, colorInt };
    _debugVertices3D[index + 1] = { v1, colorInt };

    _debugVertices3D[index + 2] = { v1, colorInt };
    _debugVertices3D[index + 3] = { v2, colorInt };

    _debugVertices3D[index + 4] = { v2, colorInt };
    _debugVertices3D[index + 5] = { v0, colorInt };
}

void DebugRenderer::DrawCircle2D(const vec2& center, f32 radius, i32 resolution, Color color)
{
    // Ensure resolution is even
    resolution += resolution % 2;

    u32 colorInt = color.ToABGR32();

    constexpr f32 PI = glm::pi<f32>();
    constexpr f32 TAU = PI * 2.0f;

    const vec2& renderSize = _renderer->GetRenderSize();
    f32 increment = TAU / resolution;

    // Each line segment (resolution of them) needs 2 vertices: start and end
    u32 vertexCount = resolution * 2;
    u32 index = _debugVertices2D.AddCount(vertexCount);

    for (i32 i = 0; i < resolution; i++)
    {
        f32 startAngle = i * increment;
        f32 endAngle = (i + 1) * increment;

        glm::vec2 posStart = glm::vec2(radius * glm::cos(startAngle) + center.x,
            radius * glm::sin(startAngle) + center.y) / renderSize;

        glm::vec2 posEnd = glm::vec2(radius * glm::cos(endAngle) + center.x,
            radius * glm::sin(endAngle) + center.y) / renderSize;

        _debugVertices2D[index + i * 2] = { posStart, colorInt };
        _debugVertices2D[index + i * 2 + 1] = { posEnd,   colorInt };
    }
}

void DebugRenderer::DrawCircle3D(const vec3& center, f32 radius, i32 resolution, Color color)
{
    // Ensure resolution is even
    resolution += resolution % 2;

    u32 colorInt = color.ToABGR32();

    constexpr f32 PI = glm::pi<f32>();
    constexpr f32 TAU = PI * 2.0f;
    f32 increment = TAU / resolution;

    // Each segment (resolution) needs 2 vertices: start and end
    u32 vertexCount = resolution * 2;
    u32 index = _debugVertices3D.AddCount(vertexCount);

    for (i32 i = 0; i < resolution; i++)
    {
        f32 startAngle = i * increment;
        f32 endAngle = (i + 1) * increment;

        vec3 posStart(radius * glm::cos(startAngle) + center.x,
            radius * glm::sin(startAngle) + center.y,
            center.z);

        vec3 posEnd(radius * glm::cos(endAngle) + center.x,
            radius * glm::sin(endAngle) + center.y,
            center.z);

        _debugVertices3D[index + i * 2] = { posStart, colorInt };
        _debugVertices3D[index + i * 2 + 1] = { posEnd,   colorInt };
    }
}

void DebugRenderer::DrawSphere3D(const vec3& center, f32 radius, i32 resolution, Color color)
{
    // Ensure resolution is even
    resolution += resolution % 2;

    u32 colorInt = color.ToABGR32();

    constexpr f32 PI = glm::pi<f32>();

    // We'll precompute all points on the sphere
    // We have (resolution+1) points along latitude and (resolution+1) along longitude
    // lat from 0 to resolution, lon from 0 to resolution
    std::vector<vec3> points;
    points.reserve((resolution + 1) * (resolution + 1));

    for (i32 lat = 0; lat <= resolution; ++lat)
    {
        f32 theta = lat * PI / resolution;
        f32 sinTheta = sin(theta);
        f32 cosTheta = cos(theta);

        for (i32 lon = 0; lon <= resolution; ++lon)
        {
            f32 phi = lon * 2.0f * PI / resolution;
            f32 sinPhi = sin(phi);
            f32 cosPhi = cos(phi);

            vec3 point;
            point.x = center.x + radius * cosPhi * sinTheta;
            point.y = center.y + radius * cosTheta;
            point.z = center.z + radius * sinPhi * sinTheta;

            points.push_back(point);
        }
    }

    // Each lat line: we connect 'resolution' horizontal segments
    // For each of the (resolution+1) lat lines, we have resolution horizontal segments
    // Each lon line: we connect 'resolution' vertical segments
    // For each of the (resolution+1) lon lines, we have resolution vertical segments
    // Total horizontal segments: (resolution+1)*resolution
    // Total vertical segments: (resolution+1)*resolution
    // Total segments: 2*(resolution+1)*resolution
    // Each segment has 2 vertices, so total vertices = 2 * 2 * (resolution+1)*resolution = 4*(resolution+1)*resolution

    u32 totalSegments = 2 * (resolution + 1) * resolution;
    u32 totalVertices = totalSegments * 2;

    u32 index = _debugVertices3D.AddCount(totalVertices);

    // Fill horizontal line segments (connect points along each latitude line)
    for (i32 lat = 0; lat <= resolution; ++lat)
    {
        for (i32 lon = 0; lon < resolution; ++lon)
        {
            u32 p1 = lat * (resolution + 1) + lon;
            u32 p2 = p1 + 1;

            _debugVertices3D[index++] = { points[p1], colorInt };
            _debugVertices3D[index++] = { points[p2], colorInt };
        }
    }

    // Fill vertical line segments (connect points along each longitude)
    for (i32 lat = 0; lat < resolution; ++lat)
    {
        for (i32 lon = 0; lon <= resolution; ++lon)
        {
            u32 p1 = lat * (resolution + 1) + lon;
            u32 p2 = p1 + (resolution + 1);

            _debugVertices3D[index++] = { points[p1], colorInt };
            _debugVertices3D[index++] = { points[p2], colorInt };
        }
    }
}

vec3 DebugRenderer::UnProject(const vec3& point, const mat4x4& m)
{
    vec4 obj = m * vec4(point, 1.0f);
    obj /= obj.w;
    return vec3(obj);
}

void DebugRenderer::DrawFrustum(const mat4x4& viewProjectionMatrix, Color color)
{
    u32 colorInt = color.ToABGR32();
    const mat4x4 m = glm::inverse(viewProjectionMatrix);

    const vec3 near0 = UnProject(vec3(-1.0f, -1.0f, 0.0f), m);
    const vec3 near1 = UnProject(vec3(+1.0f, -1.0f, 0.0f), m);
    const vec3 near2 = UnProject(vec3(+1.0f, +1.0f, 0.0f), m);
    const vec3 near3 = UnProject(vec3(-1.0f, +1.0f, 0.0f), m);

    const vec3 far0 = UnProject(vec3(-1.0f, -1.0f, 1.0f), m);
    const vec3 far1 = UnProject(vec3(+1.0f, -1.0f, 1.0f), m);
    const vec3 far2 = UnProject(vec3(+1.0f, +1.0f, 1.0f), m);
    const vec3 far3 = UnProject(vec3(-1.0f, +1.0f, 1.0f), m);

    u32 index = _debugVertices3D.AddCount(24);

    // Near plane
    _debugVertices3D[index +  0] = { near0, colorInt };
    _debugVertices3D[index +  1] = { near1, colorInt };
    _debugVertices3D[index +  2] = { near1, colorInt };
    _debugVertices3D[index +  3] = { near2, colorInt };
    _debugVertices3D[index +  4] = { near2, colorInt };
    _debugVertices3D[index +  5] = { near3, colorInt };
    _debugVertices3D[index +  6] = { near3, colorInt };
    _debugVertices3D[index +  7] = { near0, colorInt };

    // Far plane
    _debugVertices3D[index +  8] = { far0, colorInt };
    _debugVertices3D[index +  9] = { far1, colorInt };
    _debugVertices3D[index + 10] = { far1, colorInt };
    _debugVertices3D[index + 11] = { far2, colorInt };
    _debugVertices3D[index + 12] = { far2, colorInt };
    _debugVertices3D[index + 13] = { far3, colorInt };
    _debugVertices3D[index + 14] = { far3, colorInt };
    _debugVertices3D[index + 15] = { far0, colorInt };

    // Edges
    _debugVertices3D[index + 16] = { near0, colorInt };
    _debugVertices3D[index + 17] = { far0, colorInt };
    _debugVertices3D[index + 18] = { near1, colorInt };
    _debugVertices3D[index + 19] = { far1, colorInt };
    _debugVertices3D[index + 20] = { near2, colorInt };
    _debugVertices3D[index + 21] = { far2, colorInt };
    _debugVertices3D[index + 22] = { near3, colorInt };
    _debugVertices3D[index + 23] = { far3, colorInt };
}

void DebugRenderer::DrawMatrix(const mat4x4& matrix, f32 scale)
{
    const vec3 origin = vec3(matrix[3].x, matrix[3].y, matrix[3].z);

    u32 index = _debugVertices3D.AddCount(6);

    _debugVertices3D[index + 0] = { origin, 0xFF0000FF };
    _debugVertices3D[index + 1] = { origin + (vec3(matrix[0].x, matrix[0].y, matrix[0].z) * scale), 0xFF0000FF };

    _debugVertices3D[index + 2] = { origin, 0xFF00FF00 };
    _debugVertices3D[index + 3] = { origin + (vec3(matrix[1].x, matrix[1].y, matrix[1].z) * scale), 0xFF00FF00 };

    _debugVertices3D[index + 4] = { origin, 0xFFFF0000 };
    _debugVertices3D[index + 5] = { origin + (vec3(matrix[2].x, matrix[2].y, matrix[2].z) * scale), 0xFFFF0000 };
}

void DebugRenderer::DrawLineSolid2D(const vec2& from, const vec2& to, Color color, bool shaded)
{
    color.a = static_cast<f32>(shaded);
    u32 colorInt = color.ToABGR32();
    
    u32 index = _debugVerticesSolid2D.AddCount(2);
    _debugVerticesSolid2D[index + 0] = { from, colorInt };
    _debugVerticesSolid2D[index + 1] = { to, colorInt };
}

void DebugRenderer::DrawAABBSolid3D(const vec3& center, const vec3& extents, Color color, bool shaded)
{
    vec3 v0 = center - extents;
    vec3 v1 = center + extents;

    // Normals
    vec3 bottomNormal = { 0, -1, 0 };
    vec3 topNormal = { 0, 1, 0 };
    vec3 frontNormal = { 0, 0, -1 };
    vec3 backNormal = { 0, 0, 1 };
    vec3 leftNormal = { -1, 0, 0 };
    vec3 rightNormal = { 1, 0, 0 };

    color.a = static_cast<f32>(shaded);
    u32 colorInt = color.ToABGR32();
    f32 colorFloat = *reinterpret_cast<f32*>(&colorInt);

    u32 index = _debugVerticesSolid3D.AddCount(36);

    // Bottom
    _debugVerticesSolid3D[index +  0] = { vec4(v0.x, v0.y, v0.z, 0), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  1] = { vec4(v1.x, v0.y, v1.z, 0), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  2] = { vec4(v1.x, v0.y, v0.z, 0), vec4(bottomNormal, colorFloat) };

    _debugVerticesSolid3D[index +  3] = { vec4(v0.x, v0.y, v0.z, 0), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  4] = { vec4(v0.x, v0.y, v1.z, 0), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  5] = { vec4(v1.x, v0.y, v1.z, 0), vec4(bottomNormal, colorFloat) };

    // Top
    _debugVerticesSolid3D[index +  6] = { vec4(v0.x, v1.y, v0.z, 0), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index +  7] = { vec4(v1.x, v1.y, v0.z, 0), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index +  8] = { vec4(v1.x, v1.y, v1.z, 0), vec4(topNormal, colorFloat) };

    _debugVerticesSolid3D[index +  9] = { vec4(v0.x, v1.y, v0.z, 0), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index + 10] = { vec4(v1.x, v1.y, v1.z, 0), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index + 11] = { vec4(v0.x, v1.y, v1.z, 0), vec4(topNormal, colorFloat) };

    // Front
    _debugVerticesSolid3D[index + 12] = { vec4(v0.x, v0.y, v0.z, 0), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 13] = { vec4(v1.x, v1.y, v0.z, 0), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 14] = { vec4(v0.x, v1.y, v0.z, 0), vec4(frontNormal, colorFloat) };

    _debugVerticesSolid3D[index + 15] = { vec4(v0.x, v0.y, v0.z, 0), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 16] = { vec4(v1.x, v0.y, v0.z, 0), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 17] = { vec4(v1.x, v1.y, v0.z, 0), vec4(frontNormal, colorFloat) };

    // Back
    _debugVerticesSolid3D[index + 18] = { vec4(v0.x, v0.y, v1.z, 0 ), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 19] = { vec4(v0.x, v1.y, v1.z, 0 ), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 20] = { vec4(v1.x, v1.y, v1.z, 0 ), vec4(backNormal, colorFloat) };

    _debugVerticesSolid3D[index + 21] = { vec4(v0.x, v0.y, v1.z, 0 ), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 22] = { vec4(v1.x, v1.y, v1.z, 0 ), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 23] = { vec4(v1.x, v0.y, v1.z, 0 ), vec4(backNormal, colorFloat) };

    // Left
    _debugVerticesSolid3D[index + 24] = { vec4(v0.x, v0.y, v0.z, 0 ), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 25] = { vec4(v0.x, v1.y, v1.z, 0 ), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 26] = { vec4(v0.x, v0.y, v1.z, 0 ), vec4(leftNormal, colorFloat) };

    _debugVerticesSolid3D[index + 27] = { vec4(v0.x, v0.y, v0.z, 0 ), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 28] = { vec4(v0.x, v1.y, v0.z, 0 ), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 29] = { vec4(v0.x, v1.y, v1.z, 0 ), vec4(leftNormal, colorFloat) };

    // Right
    _debugVerticesSolid3D[index + 30] = { vec4(v1.x, v0.y, v0.z, 0 ), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 31] = { vec4(v1.x, v0.y, v1.z, 0 ), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 32] = { vec4(v1.x, v1.y, v1.z, 0 ), vec4(rightNormal, colorFloat) };

    _debugVerticesSolid3D[index + 33] = { vec4(v1.x, v0.y, v0.z, 0 ), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 34] = { vec4(v1.x, v1.y, v1.z, 0 ), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 35] = { vec4(v1.x, v1.y, v0.z, 0 ), vec4(rightNormal, colorFloat) };
}

void DebugRenderer::DrawOBBSolid3D(const vec3& center, const vec3& extents, const quat& rotation, Color color, bool shaded)
{
    vec3 corners[8] = {
        center + rotation * glm::vec3(-extents.x, -extents.y, -extents.z),
        center + rotation * glm::vec3(extents.x, -extents.y, -extents.z),
        center + rotation * glm::vec3(extents.x, -extents.y,  extents.z),
        center + rotation * glm::vec3(-extents.x, -extents.y,  extents.z),
        center + rotation * glm::vec3(-extents.x,  extents.y, -extents.z),
        center + rotation * glm::vec3(extents.x,  extents.y, -extents.z),
        center + rotation * glm::vec3(extents.x,  extents.y,  extents.z),
        center + rotation * glm::vec3(-extents.x,  extents.y,  extents.z)
    };

    // Normals for each face of the box (oriented with the rotation)
    vec3 bottomNormal = rotation * vec3(0, -1, 0);
    vec3 topNormal = rotation * vec3(0, 1, 0);
    vec3 frontNormal = rotation * vec3(0, 0, -1);
    vec3 backNormal = rotation * vec3(0, 0, 1);
    vec3 leftNormal = rotation * vec3(-1, 0, 0);
    vec3 rightNormal = rotation * vec3(1, 0, 0);

    color.a = static_cast<f32>(shaded);
    u32 colorInt = color.ToABGR32();
    f32 colorFloat = *reinterpret_cast<f32*>(&colorInt);

    u32 index = _debugVerticesSolid3D.AddCount(36);

    // Bottom
    _debugVerticesSolid3D[index +  0] = { vec4(corners[0], 0.0f), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  1] = { vec4(corners[2], 0.0f), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  2] = { vec4(corners[1], 0.0f), vec4(bottomNormal, colorFloat) };

    _debugVerticesSolid3D[index +  3] = { vec4(corners[0], 0.0f), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  4] = { vec4(corners[3], 0.0f), vec4(bottomNormal, colorFloat) };
    _debugVerticesSolid3D[index +  5] = { vec4(corners[2], 0.0f), vec4(bottomNormal, colorFloat) };

    // Top
    _debugVerticesSolid3D[index +  6] = { vec4(corners[4], 0.0f), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index +  7] = { vec4(corners[5], 0.0f), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index +  8] = { vec4(corners[6], 0.0f), vec4(topNormal, colorFloat) };

    _debugVerticesSolid3D[index +  9] = { vec4(corners[4], 0.0f), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index + 10] = { vec4(corners[6], 0.0f), vec4(topNormal, colorFloat) };
    _debugVerticesSolid3D[index + 11] = { vec4(corners[7], 0.0f), vec4(topNormal, colorFloat) };

    // Front
    _debugVerticesSolid3D[index + 12] = { vec4(corners[0], 0.0f), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 13] = { vec4(corners[1], 0.0f), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 14] = { vec4(corners[5], 0.0f), vec4(frontNormal, colorFloat) };

    _debugVerticesSolid3D[index + 15] = { vec4(corners[0], 0.0f), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 16] = { vec4(corners[5], 0.0f), vec4(frontNormal, colorFloat) };
    _debugVerticesSolid3D[index + 17] = { vec4(corners[4], 0.0f), vec4(frontNormal, colorFloat) };

    // Back
    _debugVerticesSolid3D[index + 18] = { vec4(corners[2], 0.0f), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 19] = { vec4(corners[3], 0.0f), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 20] = { vec4(corners[7], 0.0f), vec4(backNormal, colorFloat) };

    _debugVerticesSolid3D[index + 21] = { vec4(corners[2], 0.0f), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 22] = { vec4(corners[7], 0.0f), vec4(backNormal, colorFloat) };
    _debugVerticesSolid3D[index + 23] = { vec4(corners[6], 0.0f), vec4(backNormal, colorFloat) };

    // Left
    _debugVerticesSolid3D[index + 24] = { vec4(corners[0], 0.0f), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 25] = { vec4(corners[7], 0.0f), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 26] = { vec4(corners[3], 0.0f), vec4(leftNormal, colorFloat) };

    _debugVerticesSolid3D[index + 27] = { vec4(corners[0], 0.0f), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 28] = { vec4(corners[4], 0.0f), vec4(leftNormal, colorFloat) };
    _debugVerticesSolid3D[index + 29] = { vec4(corners[7], 0.0f), vec4(leftNormal, colorFloat) };

    // Right
    _debugVerticesSolid3D[index + 30] = { vec4(corners[1], 0.0f), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 31] = { vec4(corners[2], 0.0f), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 32] = { vec4(corners[6], 0.0f), vec4(rightNormal, colorFloat) };

    _debugVerticesSolid3D[index + 33] = { vec4(corners[1], 0.0f), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 34] = { vec4(corners[6], 0.0f), vec4(rightNormal, colorFloat) };
    _debugVerticesSolid3D[index + 35] = { vec4(corners[5], 0.0f), vec4(rightNormal, colorFloat) };
}

void DebugRenderer::DrawTriangleSolid2D(const vec2& v0, const vec2& v1, const vec2& v2, Color color, bool shaded)
{
    color.a = static_cast<f32>(shaded);
    u32 colorInt = color.ToABGR32();

    u32 index = _debugVerticesSolid2D.AddCount(6);
    _debugVerticesSolid2D[index + 0] = { v0, colorInt };
    _debugVerticesSolid2D[index + 1] = { v1, colorInt };

    _debugVerticesSolid2D[index + 2] = { v1, colorInt };
    _debugVerticesSolid2D[index + 3] = { v2, colorInt };

    _debugVerticesSolid2D[index + 4] = { v2, colorInt };
    _debugVerticesSolid2D[index + 5] = { v0, colorInt };
}

void DebugRenderer::DrawTriangleSolid3D(const vec3& v0, const vec3& v1, const vec3& v2, Color color, bool shaded)
{
    vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

    color.a = static_cast<f32>(shaded);
    u32 colorInt = color.ToABGR32();
    f32 colorFloat = *reinterpret_cast<f32*>(&colorInt);
    
    u32 index = _debugVerticesSolid3D.AddCount(6);
    _debugVerticesSolid3D[index + 0] = { vec4(v0, 0.0f), vec4(normal, colorFloat) };
    _debugVerticesSolid3D[index + 1] = { vec4(v1, 0.0f), vec4(normal, colorFloat) };

    _debugVerticesSolid3D[index + 2] = { vec4(v1, 0.0f), vec4(normal, colorFloat) };
    _debugVerticesSolid3D[index + 3] = { vec4(v2, 0.0f), vec4(normal, colorFloat) };

    _debugVerticesSolid3D[index + 4] = { vec4(v2, 0.0f), vec4(normal, colorFloat) };
    _debugVerticesSolid3D[index + 5] = { vec4(v0, 0.0f), vec4(normal, colorFloat) };
}

void DebugRenderer::RegisterCullingPassBufferUsage(Renderer::RenderGraphBuilder& builder)
{
    using BufferUsage = Renderer::BufferPassUsage;
    builder.Write(_gpuDebugVertices2D, BufferUsage::COMPUTE);
    builder.Write(_gpuDebugVertices2DArgumentBuffer, BufferUsage::COMPUTE);

    builder.Write(_gpuDebugVertices3D, BufferUsage::COMPUTE);
    builder.Write(_gpuDebugVertices3DArgumentBuffer, BufferUsage::COMPUTE);
}