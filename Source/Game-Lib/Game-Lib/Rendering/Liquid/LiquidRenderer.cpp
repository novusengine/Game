#include "LiquidRenderer.h"

#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/RenderUtils.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/RenderGraphBuilder.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_LiquidRendererEnabled(CVarCategory::Client | CVarCategory::Rendering, "liquidEnabled", "enable liquidrendering", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_LiquidCullingEnabled(CVarCategory::Client | CVarCategory::Rendering, "liquidCulling", "enable liquid culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_LiquidOcclusionCullingEnabled(CVarCategory::Client | CVarCategory::Rendering, "liquidCullingOcclusion", "enable liquid occlusion culling", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_LiquidDrawAABBs(CVarCategory::Client | CVarCategory::Rendering, "liquidDebugDrawAABBs", "if enabled, the culling pass will debug draw all AABBs", 0, CVarFlags::EditCheckbox);

AutoCVar_Float CVAR_LiquidVisibilityRange(CVarCategory::Client | CVarCategory::Rendering, "liquidVisibilityRange", "How far under a liquid you should see", 500.0f, CVarFlags::EditFloatDrag);

AutoCVar_Int CVAR_LiquidValidateTransfers(CVarCategory::Client | CVarCategory::Rendering, "liquidCalidateGPUVectors", "if enabled ON START we will validate GPUVector uploads", 0, CVarFlags::EditCheckbox);

LiquidRenderer::LiquidRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, debugRenderer)
    , _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    if (CVAR_LiquidValidateTransfers.Get())
    {
        _vertices.SetValidation(true);
        _indices.SetValidation(true);
        _cullingDatas.SetValidation(true);

        _cullingResources.SetValidation(true);
    }

    CreatePermanentResources();
}

LiquidRenderer::~LiquidRenderer()
{

}

void LiquidRenderer::Update(f32 deltaTime)
{
    if (!CVAR_LiquidRendererEnabled.Get())
        return;

    _constants.liquidVisibilityRange = CVAR_LiquidVisibilityRange.GetFloat();
    _constants.currentTime += deltaTime;

    const bool cullingEnabled = CVAR_LiquidCullingEnabled.Get();
    _cullingResources.Update(deltaTime, cullingEnabled);

    SyncToGPU();
}

void LiquidRenderer::Clear()
{
    _cullingDatas.Clear();

    _cullingResources.Clear();
    _instanceIndex.store(0);

    _vertices.Clear();
    _verticesIndex.store(0);

    _indices.Clear();
    _indicesIndex.store(0);

    _renderer->UnloadTexturesInArray(_textures, 1);
}

void LiquidRenderer::Reserve(ReserveInfo& info)
{
    _cullingResources.Grow(info.numInstances);

    _cullingDatas.Grow(info.numInstances);

    _vertices.Grow(info.numVertices);
    _indices.Grow(info.numIndices);
}

void LiquidRenderer::FitAfterGrow()
{
    // TODO
}

void LiquidRenderer::Load(LoadDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
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
    drawCallData.textureStartIndex = 0;
    drawCallData.textureCount = 1;
    drawCallData.hasDepth = 0;
    drawCallData.liquidType = 0;
    drawCallData.uvAnim = hvec2(0.0f, 0.0f);

    // Load textures if they exist
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    entt::registry::context& ctx = registry->ctx();
    auto& clientDBCollection = ctx.get<ECS::Singletons::ClientDBCollection>();
    auto* liquidTypes = clientDBCollection.Get<ClientDB::Definitions::LiquidType>(ECS::Singletons::ClientDBHash::LiquidType);

    bool isLavaOrSlime = false;
    if (liquidTypes->HasRow(desc.typeID))
    {
        const ClientDB::Definitions::LiquidType* liquidType = liquidTypes->GetRow(desc.typeID);

        u16 textureStartIndex = 0;
        u32 textureCount = liquidType->frameCountTextures[0];

        if (textureCount > std::numeric_limits<u8>().max())
        {
            NC_LOG_CRITICAL("Tried to load a liquid texture with more than 255 frames!");
        }

        {
            std::scoped_lock lock(_textureMutex);

            char textureBuffer[256];
            for (u32 i = 0; i < liquidType->frameCountTextures[0]; i++)
            {
                i32 length = StringUtils::FormatString(textureBuffer, 256, liquidType->textures[0].c_str(), i + 1);
            
                Renderer::TextureDesc textureDesc;
                textureDesc.path = "Data/Texture/" + std::string(textureBuffer, length);
            
                u32 index;
                _renderer->LoadTextureIntoArray(textureDesc, _textures, index);
            
                if (i == 0)
                {
                    textureStartIndex = static_cast<u16>(index);
                }
            }
        }

        if (liquidType->soundBank == 2 || liquidType->soundBank == 3)
        {
            isLavaOrSlime = true;
        }

        drawCallData.textureStartIndex = textureStartIndex;
        drawCallData.textureCount = textureCount;
        drawCallData.liquidType = liquidType->soundBank; // This is a workaround for now, but we don't want to rely on soundbank for knowing if this is liquid, lava or slime in the future
        drawCallData.uvAnim = hvec2(0.0f, 0.0f); // TODO: Load this from Vertex format data
    }

    vec3 min = vec3(100000, 100000, 100000);
    vec3 max = vec3(-100000, -100000, -100000);

    for (u8 y = 0; y <= desc.height; y++)
    {
        u8 yOffset = y + desc.posY;
        f32 offsetY = -(static_cast<f32>(yOffset) * Terrain::PATCH_SIZE);

        for (u8 x = 0; x <= desc.width; x++)
        {
            u8 xOffset = x + desc.posX;
            f32 offsetX = -(static_cast<f32>(xOffset) * Terrain::PATCH_SIZE);

            u32 vertexDataIndex = (8 - y) + (x * (desc.height + 1));

            f32 vertexHeight = desc.defaultHeight;
            if (desc.heightMap != nullptr)
            {
                vertexHeight = desc.heightMap[vertexDataIndex];
            }

            vec2 cellOffsetPos = vec2(yOffset * Terrain::PATCH_SIZE, xOffset * Terrain::PATCH_SIZE);
            vec3 cellPos = vec3(desc.cellPos.x - cellOffsetPos.x, vertexHeight, desc.cellPos.y - cellOffsetPos.y);

            vec2 uv = vec2(static_cast<f32>(-y) / 2.0f, static_cast<f32>(-x) / 2.0f); // These need to be inverted and swizzled

            if (isLavaOrSlime)
            {
                uv = vec2(cellPos.x * 0.06f, cellPos.z * 0.06f);
            }

            u32 vertexIndex = x + (y * (desc.width + 1));
            Vertex& vertex = vertices[vertexOffset + vertexIndex];

            // The offsets here are flipped on purpose
            vertex.xCellOffset = yOffset;
            vertex.yCellOffset = xOffset;
            vertex.height = vertexHeight;
            vertex.uv = hvec2(uv);

            if (yOffset >= desc.endY || xOffset >= desc.endX)
            {
                vec2 minWithoutHeight = glm::min(vec2(min.x, min.z), vec2(cellPos.x, cellPos.z));
                vec2 maxWithoutHeight = glm::max(vec2(max.x, max.z), vec2(cellPos.x, cellPos.z));

                min = vec3(minWithoutHeight.x, min.y, minWithoutHeight.y);
                max = vec3(maxWithoutHeight.x, max.y, maxWithoutHeight.y);
                continue;
            }

            // Check if this tile is used
            if (desc.bitMap != nullptr)
            {
                i32 maskIndex = (xOffset - desc.startX) * (desc.endY - desc.startY) + ((7 - yOffset) - desc.startY);
                bool exists = (desc.bitMap[maskIndex >> 3] >> ((maskIndex & 7))) & 1;

                if (!exists)
                    continue;
            }

            min = glm::min(min, cellPos);
            max = glm::max(max, cellPos);
        }
    }

    for (i32 y = desc.startY; y < desc.endY; y++)
    {
        for (i32 x = desc.startX; x < desc.endX; x++)
        {
            // Check if this tile is used
            if (desc.bitMap != nullptr)
            {
                i32 maskIndex = (x - desc.startX) * (desc.endY - desc.startY) + ((7 - y) - desc.startY);
                bool exists = (desc.bitMap[maskIndex >> 3] >> ((maskIndex & 7))) & 1;

                if (!exists)
                    continue;
            }

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

    // Liquids can be a flat plane so lets add offsets
    min.y -= 0.5f;
    max.y += 0.5f;

    Model::ComplexModel::CullingData& cullingData = cullingDatas[instanceOffset];
    cullingData.center = min; // TODO: Unfuck our AABB representations, we currently mix AABBs with min/max variables and ones with center/extents...
    cullingData.extents = max;
    cullingData.boundingSphereRadius = glm::distance(min, max);
}

void LiquidRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_LiquidRendererEnabled.Get())
        return;

    if (!CVAR_LiquidCullingEnabled.Get())
        return;

    if (_cullingResources.GetDrawCalls().Size() == 0)
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);

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

    renderGraph->AddPass<Data>("Liquid Culling",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            ZoneScoped;
            using BufferUsage = Renderer::BufferPassUsage;

            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cullingDatas.GetBuffer(), BufferUsage::COMPUTE);

            builder.Read(_cullingResources.GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);

            data.culledDrawCallsBuffer = builder.Write(_cullingResources.GetCulledDrawsBuffer(), BufferUsage::COMPUTE);
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
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
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

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);
            params.occlusionCull = CVAR_LiquidOcclusionCullingEnabled.Get();
            params.disableTwoStepCulling = true; // Transparent objects don't write depth, so we don't need to two step cull them

            params.modelIDIsDrawCallID = true;
            params.cullingDataIsWorldspace = true;
            params.debugDrawColliders = CVAR_LiquidDrawAABBs.Get();

            params.instanceIDOffset = 0;// offsetof(DrawCallData, instanceID);
            params.modelIDOffset = 0;// offsetof(DrawCallData, modelID);
            params.drawCallDataSize = sizeof(DrawCallData);

            CullingPass(params);
        });
}

void LiquidRenderer::AddCopyDepthPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_LiquidRendererEnabled.Get())
        return;

    struct Data
    {
        Renderer::DepthImageResource depth;
        Renderer::ImageMutableResource depthCopy;

        Renderer::DescriptorSetResource copySet;
    };

    renderGraph->AddPass<Data>("Copy Depth",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::GRAPHICS);
            data.depthCopy = builder.Write(resources.depthColorCopy, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::DISCARD);

            data.copySet = builder.Use(_copyDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
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

void LiquidRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_LiquidRendererEnabled.Get())
        return;

    if (_cullingResources.GetDrawCalls().Size() == 0)
        return;

    const bool cullingEnabled = CVAR_LiquidCullingEnabled.Get();

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

    renderGraph->AddPass<Data>("Liquid Geometry",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
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
            data.culledDrawCallsBuffer = builder.Write(_cullingResources.GetCulledDrawsBuffer(), BufferUsage::GRAPHICS);
            data.drawCountBuffer = builder.Write(_cullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
            data.triangleCountBuffer = builder.Write(_cullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
            data.drawCountReadBackBuffer = builder.Write(_cullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
            data.triangleCountReadBackBuffer = builder.Write(_cullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.drawSet = builder.Use(_cullingResources.GetGeometryPassDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, cullingEnabled](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, LiquidGeometry);

            data.drawSet.Bind("_depthRT"_h, data.depthRead);

            CulledRenderer::GeometryPassParams params;
            params.passName = "Liquid";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_cullingResources;

            params.frameIndex = frameIndex;
            params.rt0 = data.transparency;
            params.rt1 = data.transparencyWeights;
            params.depth[0] = data.depth;

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
            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);

            GeometryPass(params);
        });
}

void LiquidRenderer::CreatePermanentResources()
{
    CullingResourcesIndexed<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.bufferNamePrefix = "Liquid";
    initParams.materialPassDescriptorSet = nullptr; // Transparencies, we don't draw these in materialPass
    initParams.enableTwoStepCulling = false;
    _cullingResources.Init(initParams);

    _constants.shallowOceanColor    = Color::FromBGR32(2635575);
    _constants.deepOceanColor       = Color::FromBGR32(1387070);
    _constants.shallowRiverColor    = Color::FromBGR32(1856070);
    _constants.deepRiverColor       = Color::FromBGR32(861477);
    _constants.liquidVisibilityRange = CVAR_LiquidVisibilityRange.GetFloat();
    _constants.currentTime = 0;

    Renderer::DescriptorSet& geometrySet = _cullingResources.GetGeometryPassDescriptorSet();

    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 512;
    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    geometrySet.Bind("_textures"_h, _textures);

    Renderer::DataTextureDesc dataTextureDesc;
    dataTextureDesc.width = 1;
    dataTextureDesc.height = 1;
    dataTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM_SRGB;
    dataTextureDesc.data = new u8[4]{ 0, 0, 0, 255 }; // Black color, because liquid textures are additive
    dataTextureDesc.debugName = "LiquidDebugTexture";

    u32 arrayIndex = 0;
    _renderer->CreateDataTextureIntoArray(dataTextureDesc, _textures, arrayIndex);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::ANISOTROPIC;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;
    samplerDesc.maxAnisotropy = 8;
    _sampler = _renderer->CreateSampler(samplerDesc);
    geometrySet.Bind("_sampler"_h, _sampler);

    Renderer::SamplerDesc depthCopySamplerDesc;
    depthCopySamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;
    depthCopySamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    depthCopySamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    depthCopySamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    depthCopySamplerDesc.minLOD = 0.f;
    depthCopySamplerDesc.maxLOD = 16.f;
    depthCopySamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _depthCopySampler = _renderer->CreateSampler(samplerDesc);
    _copyDescriptorSet.Bind("_sampler"_h, _depthCopySampler);
}

void LiquidRenderer::SyncToGPU()
{
    CulledRenderer::SyncToGPU();

    // Sync Vertex buffer to GPU
    {
        _vertices.SetDebugName("LiquidVertexBuffer");
        _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_vertices.SyncToGPU(_renderer))
        {
            _cullingResources.GetGeometryPassDescriptorSet().Bind("_vertices"_h, _vertices.GetBuffer());
        }
    }

    // Sync Index buffer to GPU
    {
        _indices.SetDebugName("LiquidIndexBuffer");
        _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);

        if (_indices.SyncToGPU(_renderer))
        {
            _cullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
        }
    }

    _cullingResources.SyncToGPU();
    BindCullingResource(_cullingResources);
}

void LiquidRenderer::Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Liquid/Draw.vs.hlsl";

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Liquid/Draw.ps.hlsl";
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

    commandList.PushConstant(&_constants, 0, sizeof(Constants));

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
