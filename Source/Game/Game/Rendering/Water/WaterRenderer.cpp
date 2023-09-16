#include "WaterRenderer.h"

#include <Game/Rendering/Debug/DebugRenderer.h>
#include <Game/Rendering/RenderUtils.h>

#include <Renderer/Renderer.h>

AutoCVar_Int CVAR_WaterRendererEnabled("waterRenderer.enabled", "enable waterrendering", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_WaterCullingEnabled("waterRenderer.culling", "enable water culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_WaterOcclusionCullingEnabled("waterRenderer.culling.occlusion", "enable water occlusion culling", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_WaterDrawAABBs("waterRenderer.debug.drawAABBs", "if enabled, the culling pass will debug draw all AABBs", 0, CVarFlags::EditCheckbox);

AutoCVar_Float CVAR_WaterVisibilityRange("waterRenderer.visibilityRange", "How far underwater you should see", 3.0f, CVarFlags::EditFloatDrag);

WaterRenderer::WaterRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, debugRenderer)
    , _renderer(renderer)
	, _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

WaterRenderer::~WaterRenderer()
{

}

void WaterRenderer::Update(f32 deltaTime)
{
    if (!CVAR_WaterRendererEnabled.Get())
        return;

    const bool cullingEnabled = CVAR_WaterCullingEnabled.Get();
    _cullingResources.Update(deltaTime, cullingEnabled);

    SyncToGPU();
}

void WaterRenderer::Clear()
{
    _cullingResources.Clear();

    _cullingDatas.Clear();

    _instanceIndex.store(0);

    _vertices.Clear();
    _verticesIndex.store(0);

    _indices.Clear();
    _indicesIndex.store(0);
}

void WaterRenderer::Reserve(ReserveInfo& info)
{
    _cullingResources.Grow(info.numInstances);

    _cullingDatas.Grow(info.numInstances);

    _vertices.Grow(info.numVertices);
    _indices.Grow(info.numIndices);
}

void WaterRenderer::FitAfterGrow()
{
	// TODO
}

void WaterRenderer::Load(LoadDesc& desc)
{
    if (desc.width <= 1 || desc.height <= 1)
    {
        return;
    }

    std::vector<Renderer::IndexedIndirectDraw>& drawCalls = _cullingResources.GetDrawCalls().Get();
    std::vector<DrawCallData>& drawCallDatas = _cullingResources.GetDrawCallDatas().Get();
    std::vector<Model::ComplexModel::CullingData>& cullingDatas = _cullingDatas.Get();

    std::vector<Vertex>& vertices = _vertices.Get();
    std::vector<u16>& indices = _indices.Get();

    u32 numVertices = (desc.height + 1) * (desc.width + 1);
    u32 vertexOffset = _verticesIndex.fetch_add(numVertices);

    u32 numIndices = (desc.height) * (desc.width) * 6;
    u32 indexOffset = _indicesIndex.fetch_add(numIndices);

    u32 instanceOffset = _instanceIndex.fetch_add(1);

    Renderer::IndexedIndirectDraw& drawCall = drawCalls[instanceOffset];
    drawCall.instanceCount = 1;
    drawCall.vertexOffset = vertexOffset;
    drawCall.firstIndex = indexOffset;
    drawCall.firstInstance = instanceOffset;

    DrawCallData& drawCallData = drawCallDatas[instanceOffset];
    drawCallData.chunkID = desc.chunkID;
    drawCallData.cellID = desc.cellID;

    /*NDBC::LiquidType* liquidType = liquidTypesNDBC->GetRowById<NDBC::LiquidType>(liquidInstance.liquidTypeID);
    const std::string& liquidTexture = liquidTypesStringTable->GetString(liquidType->texture);
    u32 liquidTextureHash = liquidTypesStringTable->GetStringHash(liquidType->texture);

    u32 textureIndex;
    if (!TryLoadTexture(liquidTexture, liquidTextureHash, liquidType->numTextureFrames, textureIndex))
    {
        DebugHandler::PrintFatal("WaterRenderer::RegisterChunksToBeLoaded : failed to load texture %s", liquidTexture.c_str());
    }*/

    drawCallData.textureStartIndex = 0;//static_cast<u16>(textureIndex);
    drawCallData.textureCount = 0;//liquidType->numTextureFrames;
    drawCallData.hasDepth = 0;//liquidType->hasDepthEnabled;
    drawCallData.liquidType = 0;//liquidType->type;
    drawCallData.uvAnim = hvec2(0, 0);//hvec2(liquidType->uvAnim);

    vec3 min = vec3(100000, 100000, 100000);
    vec3 max = vec3(-100000, -100000, -100000);

    for (u8 y = 0; y <= desc.height; y++)
    {
        // This represents World (Forward/Backward) in other words, our X axis
        f32 offsetY = -(static_cast<f32>(desc.posY + y) * Terrain::PATCH_SIZE);

        for (u8 x = 0; x <= desc.width; x++)
        {
            // This represents World (West/East) in other words, our Y axis
            f32 offsetX = -(static_cast<f32>(desc.posX + x) * Terrain::PATCH_SIZE);

            f32 vertexHeight = 0.0f;// desc.liquidBasePos.z;// + liquidInstance.height;

            u32 vertexIndex = x + (y * (desc.width + 1));

            if (desc.heightMap != nullptr && desc.typeID != 2)
            {
                vertexHeight = desc.heightMap[vertexIndex];
            }

            vec2 uv = vec2(static_cast<f32>(-y) / 2.0f, static_cast<f32>(-x) / 2.0f); // These need to be inverted and swizzled

            /*if (uvEntries)
            {
                Terrain::LiquidUVMapEntry* uvEntry = &uvEntries[vertexIndex];
                uv = vec2(uvEntry->x, uvEntry->y); // This one however should not be inverted and swizzled
            }*/

            Vertex& vertex = vertices[vertexOffset + vertexIndex];
            vertex.xCellOffset = y + desc.liquidOffsetY; // These are intended to be flipped, we are going from 2D to 3D
            vertex.yCellOffset = x + desc.liquidOffsetX;
            vertex.height = f16(vertexHeight);
            vertex.uv = hvec2(uv);

            // Calculate worldspace pos for AABB usage
            vec2 pos2d = desc.cellPos -vec2(Terrain::PATCH_SIZE * (y + desc.liquidOffsetY), Terrain::PATCH_SIZE * (x + desc.liquidOffsetX));

            vec3 pos;// = desc.liquidBasePos - vec3(Terrain::PATCH_SIZE * (y + desc.liquidOffsetY), Terrain::PATCH_SIZE * (x + desc.liquidOffsetX), 0.0f);
            pos.x = pos2d.x;
            pos.y = vertexHeight;
            pos.z = pos2d.y;
            
            min = glm::min(min, pos);
            max = glm::max(max, pos);

            if (y < desc.height && x < desc.width)
            {
                // Check if this tile is used
                /*if (desc.bitMap != nullptr)
                {
                    u8& bitmap = desc.bitMap[desc.bitmapDataOffset + y];
                
                    if ((bitmap & (1 << x)) == 0)
                        continue;
                }*/

                u16 topLeftVert = x + (y * (desc.width + 1));
                u16 topRightVert = topLeftVert + 1;
                u16 bottomLeftVert = topLeftVert + (desc.width + 1);
                u16 bottomRightVert = bottomLeftVert + 1;

                indices[indexOffset++] = topLeftVert;
                indices[indexOffset++] = bottomLeftVert;
                indices[indexOffset++] = topRightVert;

                indices[indexOffset++] = topRightVert;
                indices[indexOffset++] = bottomLeftVert;
                indices[indexOffset++] = bottomRightVert;

                drawCall.indexCount += 6;
            }
        }
    }

    // Water can be a flat plane so lets add offsets
    min.y -= 0.5f;
    max.y += 0.5f;

    Model::ComplexModel::CullingData& cullingData = cullingDatas[instanceOffset];
    cullingData.center = min; // TODO: Unfuck our AABB representations, we currently mix AABBs with min/max variables and ones with center/extents...
    cullingData.extents = max;
    cullingData.boundingSphereRadius = glm::distance(min, max);

    if (cullingData.boundingSphereRadius > 100.0f)
    {
        volatile int asd = 123;
    }
    if (cullingData.boundingSphereRadius > 1000.0f)
    {
        volatile int asdf = 123;
    }
    if (cullingData.boundingSphereRadius > 10000.0f)
    {
        volatile int asdg = 123;
    }
}

void WaterRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_WaterRendererEnabled.Get())
        return;

    if (!CVAR_WaterCullingEnabled.Get())
        return;

    if (_cullingResources.GetDrawCalls().Size() == 0)
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

    struct Data
    {
        Renderer::ImageResource depthPyramid;

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource cullingSet;
    };

    renderGraph->AddPass<Data>("Water Culling",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            ZoneScoped;
            using BufferUsage = Renderer::BufferPassUsage;

            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cullingDatas.GetBuffer(), BufferUsage::COMPUTE);

            builder.Read(_cullingResources.GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);

            data.culledDrawCallsBuffer = builder.Write(_cullingResources.GetCulledDrawsBuffer(0), BufferUsage::COMPUTE);
            data.drawCountBuffer = builder.Write(_cullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.triangleCountBuffer = builder.Write(_cullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.drawCountReadBackBuffer = builder.Write(_cullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
            data.triangleCountReadBackBuffer = builder.Write(_cullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

            data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.cullingSet = builder.Use(_cullingResources.GetCullingDescriptorSet());

            _debugRenderer->RegisterCullingPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelCulling);

            CulledRenderer::CullingPassParams params;
            params.passName = "";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_cullingResources;
            params.frameIndex = frameIndex;

            params.depthPyramid = data.depthPyramid;

            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.debugDescriptorSet = data.debugSet;
            params.globalDescriptorSet = data.globalSet;
            params.cullingDescriptorSet = data.cullingSet;

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");
            params.occlusionCull = CVAR_WaterOcclusionCullingEnabled.Get();
            params.disableTwoStepCulling = true; // Transparent objects don't write depth, so we don't need to two step cull them

            params.modelIDIsDrawCallID = true;
            params.cullingDataIsWorldspace = true;
            params.debugDrawColliders = CVAR_WaterDrawAABBs.Get();

            params.instanceIDOffset = 0;// offsetof(DrawCallData, instanceID);
            params.modelIDOffset = 0;// offsetof(DrawCallData, modelID);
            params.drawCallDataSize = sizeof(DrawCallData);

            CullingPass(params);
        });
}

void WaterRenderer::AddCopyDepthPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_WaterRendererEnabled.Get())
        return;

    struct Data
    {
        Renderer::DepthImageResource depth;
        Renderer::ImageMutableResource depthCopy;

        Renderer::DescriptorSetResource copySet;
    };

    renderGraph->AddPass<Data>("Copy Depth",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::GRAPHICS);
            data.depthCopy = builder.Write(resources.depthColorCopy, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::DISCARD);

            data.copySet = builder.Use(_copyDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, CopyDepth);

            RenderUtils::CopyDepthToColorParams copyParams;
            copyParams.source = data.depth;
            copyParams.destination = data.depthCopy;
            copyParams.destinationMip = 0;
            copyParams.descriptorSet = data.copySet;

            RenderUtils::CopyDepthToColor(_renderer, graphResources, commandList, frameIndex, copyParams);
        });
}

void WaterRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_WaterRendererEnabled.Get())
        return;

    if (_cullingResources.GetDrawCalls().Size() == 0)
        return;

    const bool cullingEnabled = CVAR_WaterCullingEnabled.Get();

    struct Data
    {
        Renderer::ImageMutableResource transparency;
        Renderer::ImageMutableResource transparencyWeights;
        Renderer::DepthImageMutableResource depth;

        Renderer::ImageResource depthRead;

        Renderer::BufferMutableResource drawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Water Geometry",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.transparency = builder.Write(resources.transparency, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            
            data.depthRead = builder.Read(resources.depthColorCopy, Renderer::PipelineType::GRAPHICS);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);

            builder.Read(_cullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

            data.drawCallsBuffer = builder.Write(_cullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
            data.culledDrawCallsBuffer = builder.Write(_cullingResources.GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS);
            data.drawCountBuffer = builder.Write(_cullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
            data.triangleCountBuffer = builder.Write(_cullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
            data.drawCountReadBackBuffer = builder.Write(_cullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
            data.triangleCountReadBackBuffer = builder.Write(_cullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.drawSet = builder.Use(_cullingResources.GetGeometryPassDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, WaterGeometry);

            data.drawSet.Bind("_depthRT"_h, data.depthRead);

            CulledRenderer::GeometryPassParams params;
            params.passName = "Water";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_cullingResources;

            params.frameIndex = frameIndex;
            params.rt0 = data.transparency;
            params.rt1 = data.transparencyWeights;
            params.depth = data.depth;

            params.drawCallsBuffer = data.drawCallsBuffer;
            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.globalDescriptorSet = data.globalSet;
            params.drawDescriptorSet = data.drawSet;

            params.drawCallback = [&](const DrawParams& drawParams)
            {
                Draw(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.enableDrawing = true;//CVAR_ModelDrawGeometry.Get();
            params.cullingEnabled = cullingEnabled;
            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

            GeometryPass(params);
        });
}

void WaterRenderer::CreatePermanentResources()
{
    CullingResources<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.bufferNamePrefix = "Water";
    initParams.materialPassDescriptorSet = nullptr; // Transparencies, we don't draw these in materialPass
    initParams.enableTwoStepCulling = false;
    _cullingResources.Init(initParams);
}

void WaterRenderer::SyncToGPU()
{
    CulledRenderer::SyncToGPU();

    // Sync Vertex buffer to GPU
    {
        _vertices.SetDebugName("WaterVertexBuffer");
        _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_vertices.SyncToGPU(_renderer))
        {
            _cullingResources.GetGeometryPassDescriptorSet().Bind("_vertices"_h, _vertices.GetBuffer());
        }
    }

    // Sync Index buffer to GPU
    {
        _indices.SetDebugName("WaterIndexBuffer");
        _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);

        if (_indices.SyncToGPU(_renderer))
        {
            _cullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
        }
    }

    _cullingResources.SyncToGPU();
    SetupCullingResource(_cullingResources);
}

void WaterRenderer::Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Water/Draw.vs.hlsl";

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Water/Draw.ps.hlsl";
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

    // Blend state
    pipelineDesc.states.blendState.independentBlendEnable = true;

    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].blendOpAlpha = Renderer::BlendOp::ADD;

    pipelineDesc.states.blendState.renderTargets[1].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[1].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[1].srcBlend = Renderer::BlendMode::ZERO;
    pipelineDesc.states.blendState.renderTargets[1].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[1].srcBlendAlpha = Renderer::BlendMode::ZERO;
    pipelineDesc.states.blendState.renderTargets[1].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[1].blendOpAlpha = Renderer::BlendOp::ADD;

    // Render targets
    pipelineDesc.renderTargets[0] = params.rt0;
    if (params.rt1 != Renderer::ImageMutableResource::Invalid())
    {
        pipelineDesc.renderTargets[1] = params.rt1;
    }
    pipelineDesc.depthStencil = params.depth;

    // Draw
    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
    commandList.BeginPipeline(pipeline);

    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.drawDescriptorSet, frameIndex);

    struct Constants
    {
        Color shallowOceanColor;
        Color deepOceanColor;
        Color shallowRiverColor;
        Color deepRiverColor;
        float waterVisibilityRange;
        float currentTime;
    };
    Constants* constants = graphResources.FrameNew<Constants>();

    constants->shallowOceanColor    = Color(28.0f / 255.0f, 163.0f / 255.0f, 236.0f / 255.0f, 0.5f);
    constants->deepOceanColor       = Color(15.0f / 255.0f, 94.0f / 255.0f, 156.0f / 255.0f, 0.9f);
    constants->shallowRiverColor    = Color(116.0f / 255.0f, 204.0f / 255.0f, 244.0f / 255.0f, 0.5f);
    constants->deepRiverColor       = Color(35.0f / 255.0f, 137.0f / 255.0f, 218.0f / 255.0f, 0.9f);
    constants->waterVisibilityRange = CVAR_WaterVisibilityRange.GetFloat();
    constants->currentTime = 0.0f; // TODO
    commandList.PushConstant(constants, 0, sizeof(Constants));

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

    if (params.cullingEnabled)
    {
        u32 drawCountBufferOffset = params.drawCountIndex * sizeof(u32);
        commandList.DrawIndexedIndirectCount(params.argumentBuffer, 0, params.drawCountBuffer, drawCountBufferOffset, params.numMaxDrawCalls);
    }
    else
    {
        commandList.DrawIndexedIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
    }

    commandList.EndPipeline(pipeline);
}
