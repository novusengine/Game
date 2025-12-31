#include "LiquidRenderer.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderUtils.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

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

LiquidRenderer::LiquidRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, gameRenderer, debugRenderer)
    , _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
    , _copyDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
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
    ZoneScoped;

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

    _vertices.Clear();
    _indices.Clear();
}

void LiquidRenderer::Reserve(const ReserveInfo& info, LiquidReserveOffsets& reserveOffsets)
{
    u32 cullingResourcesStartIndex = _cullingResources.AddCount(info.numInstances);
    u32 cullingDatasStartIndex = _cullingDatas.AddCount(info.numInstances);

#if NC_DEBUG
    if (cullingResourcesStartIndex != cullingDatasStartIndex)
    {
        NC_LOG_ERROR("LiquidRenderer::Reserve: Culling resources start index %u does not match culling data start index %u, this will probably result in weird liquid", cullingResourcesStartIndex, cullingDatasStartIndex);
    }
#endif

    reserveOffsets.instanceStartOffset = cullingResourcesStartIndex;
    reserveOffsets.vertexStartOffset = _vertices.AddCount(info.numVertices);
    reserveOffsets.indexStartOffset = _indices.AddCount(info.numIndices);

    _instancesIsDirty = true;
}

void LiquidRenderer::Load(LoadDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
    {
        return;
    }

    // Shared lock as we don't do anything that causes reallocation
    std::shared_lock lock(_addLiquidMutex);

    const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& drawCalls = _cullingResources.GetDrawCalls();
    const Renderer::GPUVector<DrawCallData>& drawCallDatas = _cullingResources.GetDrawCallDatas();

    Renderer::IndexedIndirectDraw& drawCall = drawCalls[desc.instanceOffset];
    drawCall.instanceCount = 1;
    drawCall.vertexOffset = desc.vertexOffset;
    drawCall.firstIndex = desc.indexOffset;
    drawCall.firstInstance = desc.instanceOffset;

    DrawCallData& drawCallData = drawCallDatas[desc.instanceOffset];
    drawCallData.chunkID = desc.chunkID;
    drawCallData.cellID = desc.cellID;
    drawCallData.textureStartIndex = 0;
    drawCallData.textureCount = 1;
    drawCallData.hasDepth = 0;
    drawCallData.liquidType = 0;
    drawCallData.uvAnim = hvec2(0.0f, 0.0f);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    entt::registry::context& ctx = registry->ctx();
    auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
    auto* liquidTypes = clientDBSingleton.Get(ClientDBHash::LiquidType);

    bool isLavaOrSlime = false;
    if (liquidTypes->Has(desc.typeID))
    {
        const auto& liquidType = liquidTypes->Get<MetaGen::Shared::ClientDB::LiquidTypeRecord>(desc.typeID);

        const auto& liquidTextureMap = _liquidTypeIDToLiquidTextureMap[desc.typeID];
        drawCallData.textureStartIndex = liquidTextureMap.baseTextureIndex;
        drawCallData.textureCount = liquidTextureMap.numFramesForTexture;
        drawCallData.liquidType = liquidType.soundBank; // This is a workaround for now, but we don't want to rely on soundbank for knowing if this is liquid, lava or slime in the future
        drawCallData.uvAnim = hvec2(0.0f, 0.0f); // TODO: Load this from Vertex format data

        isLavaOrSlime = liquidType.soundBank == 2 || liquidType.soundBank == 3;
    }

    // Write vertices and calculate bounding box
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
            Vertex& vertex = _vertices[desc.vertexOffset + vertexIndex];

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

    // Write indices
    u32 indexOffset = desc.indexOffset;
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

            _indices[indexOffset++] = topLeftVert;
            _indices[indexOffset++] = bottomLeftVert;
            _indices[indexOffset++] = topRightVert;

            _indices[indexOffset++] = topRightVert;
            _indices[indexOffset++] = bottomLeftVert;
            _indices[indexOffset++] = bottomRightVert;

            drawCall.indexCount += 6;
        }
    }

    // Liquids can be a flat plane so lets add offsets to the bounding box
    min.y -= 0.5f;
    max.y += 0.5f;

    // Write culling data
    Model::ComplexModel::CullingData& cullingData = _cullingDatas[desc.instanceOffset];
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

    if (_cullingResources.GetDrawCalls().Count() == 0)
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
        Renderer::DescriptorSetResource createIndirectAfterCullSet;
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

            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::COMPUTE);

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
            params.createIndirectAfterCullSet = data.cullingSet;

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

            RenderUtils::CopyDepthToColor(graphResources, commandList, frameIndex, copyParams);
        });
}

void LiquidRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_LiquidRendererEnabled.Get())
        return;

    if (_cullingResources.GetDrawCalls().Count() == 0)
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

            params.drawCallback = [&](DrawParams& drawParams)
            {
                drawParams.descriptorSets = {
                    &data.globalSet,
                    &data.drawSet
                };

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
    CreatePipelines();

    CullingResourcesIndexed<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.culledRenderer = this;
    initParams.bufferNamePrefix = "Liquid";
    initParams.materialPassDescriptorSet = nullptr; // Transparencies, we don't draw these in materialPass
    initParams.enableTwoStepCulling = false;
    initParams.isInstanced = false;
    _cullingResources.Init(initParams);

    InitDescriptorSets();

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

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    entt::registry::context& ctx = registry->ctx();
    auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
    auto* liquidTypes = clientDBSingleton.Get(ClientDBHash::LiquidType);

    u32 numLiquidTypes = liquidTypes->GetNumRows();
    _liquidTypeIDToLiquidTextureMap.reserve(numLiquidTypes);

    char textureNameBuffer[512] = { 0 };
    liquidTypes->Each([this, &liquidTypes, &textureNameBuffer](u32 id, const MetaGen::Shared::ClientDB::LiquidTypeRecord& liquidType)
    {
        // Ensure we always have a base index for all liquidTypes, default to 0
        _liquidTypeIDToLiquidTextureMap[id] =
        {
            .baseTextureIndex = 0,
            .numFramesForTexture = 1
        };

        const std::string& baseTexture = liquidTypes->GetString(liquidType.textures[0]);
        u32 numFramesForBaseTexture = liquidType.frameCounts[0];

        if (baseTexture.length() == 0 || numFramesForBaseTexture == 0)
            return true;

        if (numFramesForBaseTexture > std::numeric_limits<u8>().max())
        {
            NC_LOG_ERROR("Tried to load a liquid texture with more than 255 frames! (\"{0}\", {1})", baseTexture, numFramesForBaseTexture);
            return true;
        }

        u32 baseTextureIndex = 0;
        for (u32 i = 0; i < numFramesForBaseTexture; i++)
        {
            i32 length = StringUtils::FormatString(textureNameBuffer, 512, baseTexture.c_str(), i + 1);
            std::string texturePath = "Data/Texture/" + std::string(textureNameBuffer, length);

            Renderer::TextureDesc textureDesc =
            {
                .path = std::move(texturePath)
            };

            u32 index;
            _renderer->LoadTextureIntoArray(textureDesc, _textures, index);

            if (i == 0)
                baseTextureIndex = static_cast<u16>(index);
        }

        _liquidTypeIDToLiquidTextureMap[id] =
        {
            .baseTextureIndex = baseTextureIndex,
            .numFramesForTexture = numFramesForBaseTexture
        };

        _instanceMatrices.SetDebugName("InstanceMatricesLiquid");
        _instanceMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        return true;
    });
}

void LiquidRenderer::CreatePipelines()
{
    Renderer::GraphicsPipelineDesc pipelineDesc;

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Liquid/Draw.vs"_h, "Liquid/Draw.vs");
    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Liquid/Draw.ps"_h, "Liquid/Draw.ps");
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
    pipelineDesc.states.renderTargetFormats[0] = Renderer::ImageFormat::R16G16B16A16_FLOAT;
    pipelineDesc.states.renderTargetFormats[1] = Renderer::ImageFormat::R16_FLOAT;

    pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

    _liquidPipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
}

void LiquidRenderer::InitDescriptorSets()
{
    Renderer::DescriptorSet& geometryPassDescriptorSet = _cullingResources.GetGeometryPassDescriptorSet();
    geometryPassDescriptorSet.RegisterPipeline(_renderer, _liquidPipeline);
    geometryPassDescriptorSet.Init(_renderer);

    _copyDescriptorSet.RegisterPipeline(_renderer, RenderUtils::GetCopyDepthToColorPipeline());
    _copyDescriptorSet.Init(_renderer);
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

        }
    }

    // Really there shouldn't be instanceMatrices, but we need to bind them
    if (_instanceMatrices.SyncToGPU(_renderer))
    {
        _cullingResources.GetCullingDescriptorSet().Bind("_instanceMatrices", _instanceMatrices.GetBuffer());
    }

    _cullingResources.SyncToGPU(_instancesIsDirty);
    BindCullingResource(_cullingResources);
}

void LiquidRenderer::Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.rt0;
    if (params.rt1 != Renderer::ImageMutableResource::Invalid())
    {
        renderPassDesc.renderTargets[1] = params.rt1;
    }
    renderPassDesc.depthStencil = params.depth;
    commandList.BeginRenderPass(renderPassDesc);

    // Draw
    Renderer::GraphicsPipelineID pipeline = _liquidPipeline;
    commandList.BeginPipeline(pipeline);

    for (auto& descriptorSet : params.descriptorSets)
    {
        commandList.BindDescriptorSet(*descriptorSet, frameIndex);
    }

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
    commandList.EndRenderPass(renderPassDesc);
}
