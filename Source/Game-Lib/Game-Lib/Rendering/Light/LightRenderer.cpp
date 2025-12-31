#include "LightRenderer.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Decal.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/TerrainTools.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/RenderUtils.h"
#include "Game-Lib/Rendering/Terrain/TerrainRenderer.h"
#include "Game-Lib/Util/PhysicsUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

AutoCVar_ShowFlag CVAR_DebugLightTiles(CVarCategory::Client | CVarCategory::Rendering, "debugLightTiles", "Debug draw light tiles", ShowFlag::DISABLED);

LightRenderer::LightRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer, ModelRenderer* modelRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
    , _modelRenderer(modelRenderer)
    , _classifyPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _debugPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    CreatePermanentResources();
}

LightRenderer::~LightRenderer()
{

}

void LightRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    // Handle remove requests
    u32 numDecalRemovals = static_cast<u32>(_decalRemoveRequests.try_dequeue_bulk(_decalRemoveWork.begin(), 64));
    if (numDecalRemovals > 0)
    {
        for (u32 i = 0; i < numDecalRemovals; i++)
        {
            DecalRemoveRequest& request = _decalRemoveWork[i];
            auto it = _entityToDecalID.find(request.entity);
            if (it == _entityToDecalID.end())
            {
                continue;
            }

            u32 decalID = it->second;
            _decals.Remove(decalID);
            _decalIDToEntity.erase(decalID);
            _entityToDecalID.erase(it);
        }
    }

    // Update existing decals if dirty transform
    auto dirtyDecalTransformView = registry->view<ECS::Components::Transform, ECS::Components::AABB, ECS::Components::Decal, ECS::Components::DirtyTransform>();
    dirtyDecalTransformView.each([this, &registry](entt::entity entity, ECS::Components::Transform& transform, ECS::Components::AABB& aabb, ECS::Components::Decal& decalComp, ECS::Components::DirtyTransform& dirtyTransform)
    {
        auto it = _entityToDecalID.find(entity);
        if (it == _entityToDecalID.end())
        {
            // New decal we didn't know about, should we maybe just register it? For now it requires explicit AddDecal call
            return;
        }

        u32 decalID = it->second;
        GPUDecal& decal = _decals[decalID];

        vec3 position = transform.GetWorldPosition() + aabb.centerPos;
        decal.positionAndTextureID = vec4(position, decal.positionAndTextureID.w);
        decal.rotation = transform.GetWorldRotation();
        
        _decals.SetDirtyElement(decalID);
    });

    // Update existing decals if dirty AABB
    auto dirtyDecalAABBView = registry->view<ECS::Components::Transform, ECS::Components::AABB, ECS::Components::Decal, ECS::Components::DirtyAABB>();
    dirtyDecalAABBView.each([this, &registry](entt::entity entity, ECS::Components::Transform& transform, ECS::Components::AABB& aabb, ECS::Components::Decal& decalComp)
    {
        auto it = _entityToDecalID.find(entity);
        if (it == _entityToDecalID.end())
        {
            // New decal we didn't know about, should we maybe just register it? For now it requires explicit AddDecal call
            return;
        }

        u32 decalID = it->second;
        GPUDecal& decal = _decals[decalID];

        vec3 position = transform.GetWorldPosition() + aabb.centerPos;
        decal.positionAndTextureID = vec4(position, decal.positionAndTextureID.w);
        decal.extentsAndColor = vec4(aabb.extents, decal.extentsAndColor.w);

        _decals.SetDirtyElement(decalID);
    });

    // Update existing decals if dirty decal settings
    auto dirtyDecalView = registry->view<ECS::Components::Transform, ECS::Components::AABB, ECS::Components::Decal, ECS::Components::DirtyDecal>();
    dirtyDecalView.each([this, &registry](entt::entity entity, ECS::Components::Transform& transform, ECS::Components::AABB& aabb, ECS::Components::Decal& decalComp)
    {
        auto it = _entityToDecalID.find(entity);
        if (it == _entityToDecalID.end())
        {
            // New decal we didn't know about, should we maybe just register it? For now it requires explicit AddDecal call
            return;
        }

        u32 decalID = it->second;
        GPUDecal& decal = _decals[decalID];

        // Load the texture into the model renderer (bigger chance for texture reuse than having our separate texture array)
        _modelRenderer->LoadTexture(decalComp.texturePath, *reinterpret_cast<u32*>(&decal.positionAndTextureID.w));

        u32 colorInt = decalComp.colorMultiplier.ToABGR32();

        decal.extentsAndColor.w = *reinterpret_cast<f32*>(&colorInt);
        decal.thresholdMinMax = decalComp.thresholdMinMax;
        decal.minUV = decalComp.minUV;
        decal.maxUV = decalComp.maxUV;
        decal.flags = decalComp.flags;

        _decals.SetDirtyElement(decalID);
    });
    registry->clear<ECS::Components::DirtyDecal>();

    // Handle add requests
    u32 numDecalAdds = static_cast<u32>(_decalAddRequests.try_dequeue_bulk(_decalAddWork.begin(), 64));
    if (numDecalAdds > 0)
    {
        for (u32 i = 0; i < numDecalAdds; i++)
        {
            DecalAddRequest& request = _decalAddWork[i];

            ECS::Components::Transform& transform = registry->get<ECS::Components::Transform>(request.entity);
            ECS::Components::AABB& aabb = registry->get<ECS::Components::AABB>(request.entity);
            ECS::Components::Decal& decalComp = registry->get<ECS::Components::Decal>(request.entity);

            // Load the texture into the model renderer (bigger chance for texture reuse than having our separate texture array)
            u32 textureID;
            _modelRenderer->LoadTexture(decalComp.texturePath, textureID);

            // Add decal
            vec3 position = transform.GetWorldPosition() + aabb.centerPos;
            quat rotation = transform.GetWorldRotation();

            u32 colorInt = decalComp.colorMultiplier.ToABGR32();

            GPUDecal decal
            {
                .positionAndTextureID = vec4(position, *reinterpret_cast<f32*>(&textureID)),
                .rotation = rotation,
                .extentsAndColor = vec4(aabb.extents, *reinterpret_cast<f32*>(&colorInt)),
                .thresholdMinMax = decalComp.thresholdMinMax,
                .minUV = decalComp.minUV,
                .maxUV = decalComp.maxUV,
                .flags = decalComp.flags,
            };

            u32 decalID = _decals.Add(decal);
            _decalIDToEntity[decalID] = request.entity;
            _entityToDecalID[request.entity] = decalID;
        }
    }

    SyncToGPU();

    // Debug draw decals as wireframes
    if (CVAR_DebugLightTiles.Get() == ShowFlag::ENABLED)
    {
        for (u32 i = 0; i < _decals.Count(); i++)
        {
            const GPUDecal& decal = _decals[i];

            vec3 min = vec3(decal.positionAndTextureID) - vec3(decal.extentsAndColor);
            vec3 max = vec3(decal.positionAndTextureID) + vec3(decal.extentsAndColor);

            _debugRenderer->DrawOBB3D(vec3(decal.positionAndTextureID), vec3(decal.extentsAndColor), decal.rotation, Color::Cyan);
        }
    }

    /*ECS::Util::EventUtil::OnEvent<ECS::Components::MapLoadedEvent>([&](const ECS::Components::MapLoadedEvent& event)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

        entt::entity decalEntity = registry->create();

        ECS::TransformSystem& ts = ECS::TransformSystem::Get(*registry);

        registry->emplace<ECS::Components::Name>(decalEntity, "Test Decal");

        registry->emplace<ECS::Components::Transform>(decalEntity);
        ts.SetWorldPosition(decalEntity, vec3(-1414.81f, 343.78f, 1012.89f));
        ts.SetWorldRotation(decalEntity, quat(vec3(glm::radians(90.0f), 0.0f, 0.0f)));
        registry->emplace<ECS::Components::AABB>(decalEntity, vec3(0.0f), vec3(5.0f, 5.0f, 10.0f));

        auto& decal = registry->emplace<ECS::Components::Decal>(decalEntity);
        decal.texturePath = "Data/Texture/interface/spellshadow/spell-shadow-acceptable.dds";

        registry->emplace<ECS::Components::DirtyDecal>(decalEntity);

        AddDecal(decalEntity);
    });*/
}

void LightRenderer::Clear()
{
    DecalAddRequest decalAddRequest;
    while (_decalAddRequests.try_dequeue(decalAddRequest)) {}

    DecalRemoveRequest decalRemoveRequest;
    while (_decalRemoveRequests.try_dequeue(decalRemoveRequest)) {}

    _decals.Clear();
    _decalIDToEntity.clear();
    _entityToDecalID.clear();
}

void LightRenderer::AddClassificationPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct ClassificationPassData
    {
        Renderer::BufferMutableResource entityTilesBuffer;

        Renderer::DepthImageResource depth;

        Renderer::ImageMutableResource debugColor;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource classifySet;
    };

    renderGraph->AddPass<ClassificationPassData>("Light Classification",
        [this, &resources](ClassificationPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.entityTilesBuffer = builder.Write(_entityTilesBuffer, Renderer::BufferPassUsage::COMPUTE | Renderer::BufferPassUsage::TRANSFER);

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);

            data.debugColor = builder.Write(resources.debugColor, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::CLEAR);

            builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
            builder.Read(_decals.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);

            _debugRenderer->RegisterCullingPassBufferUsage(builder);

            data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.classifySet = builder.Use(_classifyPassDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](ClassificationPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, LightClassificationPass);

            Renderer::ComputePipelineID pipeline = _classificationPipeline;
            commandList.BeginPipeline(pipeline);

            uvec2 outputSize = _renderer->GetImageDimensions(resources.sceneColor, 0);
            u32 numTilesX = (outputSize.x + TILE_SIZE - 1) / TILE_SIZE;
            u32 numTilesY = (outputSize.y + TILE_SIZE - 1) / TILE_SIZE;
            uvec2 tileCount = uvec2(numTilesX, numTilesY);

            struct Constants
            {
                u32 maxEntitiesPerTile;
                u32 numTotalDecals;
                uvec2 tileCount;
                vec2 screenSize;
            };

            Constants* constants = graphResources.FrameNew<Constants>();
            constants->maxEntitiesPerTile = MAX_ENTITIES_PER_TILE;
            constants->numTotalDecals = static_cast<u32>(_decals.Count());
            constants->tileCount = tileCount;
            constants->screenSize = static_cast<vec2>(outputSize);

            commandList.PushConstant(constants, 0, sizeof(Constants));

            data.classifySet.Bind("_depthRT", data.depth);
            //data.classifySet.Bind("_debugTexture", data.debugColor);

            // Bind descriptorset
            commandList.BindDescriptorSet(data.debugSet, frameIndex);
            commandList.BindDescriptorSet(data.globalSet, frameIndex);
            commandList.BindDescriptorSet(data.classifySet, frameIndex);

            commandList.Dispatch(numTilesX, numTilesY, 1);

            commandList.EndPipeline(pipeline);
        });
}

void LightRenderer::AddDebugPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (CVAR_DebugLightTiles.Get() == ShowFlag::DISABLED)
        return;

    struct DebugPassData
    {
        Renderer::ImageMutableResource color;
        Renderer::ImageResource debug;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource debugSet;
    };
    renderGraph->AddPass<DebugPassData>("Light Debug Overlay",
        [this, &resources](DebugPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.color = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.debug = builder.Read(resources.debugColor, Renderer::PipelineType::GRAPHICS);

            builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.debugSet = builder.Use(_debugPassDescriptorSet);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, &resources](DebugPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, DecalDebugOverlay);

            RenderUtils::OverlayParams overlayParams;
            overlayParams.baseImage = data.color;
            overlayParams.overlayImage = data.debug;
            overlayParams.descriptorSet = data.debugSet;

            RenderUtils::Overlay(graphResources, commandList, frameIndex, overlayParams);
        });
}

void LightRenderer::AddDecal(entt::entity entity)
{
    // Make sure entity has required components
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    if (!registry->all_of<ECS::Components::Transform, ECS::Components::AABB, ECS::Components::Decal>(entity))
    {
        return;
    }

    DecalAddRequest request
    {
        .entity = entity,
    };
    _decalAddRequests.enqueue(request);
}

void LightRenderer::RemoveDecal(entt::entity entity)
{
    DecalRemoveRequest request
    {
        .entity = entity,
    };
    _decalRemoveRequests.enqueue(request);
}

void LightRenderer::CreatePermanentResources()
{
    // Create pipeline
    Renderer::ComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "LightClassification";

    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Light/Classification.cs"_h, "Light/Classification.cs");
    pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

    _classificationPipeline = _renderer->CreatePipeline(pipelineDesc);

    // Create descriptor sets
    _classifyPassDescriptorSet.RegisterPipeline(_renderer, _classificationPipeline);
    _classifyPassDescriptorSet.Init(_renderer);

    _decals.SetDebugName("Decals");
    _decals.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _renderer->AddOnRenderSizeChanged([this](const vec2& newSize)
    {
        RecreateBuffer(newSize);
    });

    _decalAddWork.resize(64);
    _decalRemoveWork.resize(64);

    // Init debug pass descriptor set
    std::string componentTypeName = Renderer::GetTextureTypeName(Renderer::ImageFormat::B8G8R8A8_UNORM_SRGB);
    u32 componentTypeNameHash = StringUtils::fnv1a_32(componentTypeName.c_str(), componentTypeName.size());
    Renderer::GraphicsPipelineID debugPipeline = _gameRenderer->GetOverlayPipeline(componentTypeNameHash);
    _debugPassDescriptorSet.RegisterPipeline(_renderer, debugPipeline);
    _debugPassDescriptorSet.Init(_renderer);
}

u32 LightRenderer::CalculateNumTiles(const vec2& size)
{
    uvec2 numTiles2D = CalculateNumTiles2D(size);
    return numTiles2D.x * numTiles2D.y;
}

uvec2 LightRenderer::CalculateNumTiles2D(const vec2& size)
{
    u32 width = static_cast<u32>(size.x);
    u32 height = static_cast<u32>(size.y);

    u32 numTilesX = (width + TILE_SIZE - 1) / TILE_SIZE;
    u32 numTilesY = (height + TILE_SIZE - 1) / TILE_SIZE;
    return uvec2(numTilesX, numTilesY);
}

void LightRenderer::RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder)
{
    builder.Read(_entityTilesBuffer, Renderer::BufferPassUsage::COMPUTE);
    builder.Read(_decals.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
}

void LightRenderer::RecreateBuffer(const vec2& size)
{
    RenderResources& resources = _gameRenderer->GetRenderResources();

    u32 numTiles = CalculateNumTiles(size);

    Renderer::BufferDesc bufferDesc;
    bufferDesc.name = "Entity Tiles";
    bufferDesc.size = numTiles * sizeof(u32) * MAX_ENTITIES_PER_TILE * 2; // *2: opaque and transparent arrays
    bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
    bufferDesc.cpuAccess = Renderer::BufferCPUAccess::AccessNone;

    _entityTilesBuffer = _renderer->CreateBuffer(_entityTilesBuffer, bufferDesc);
    _classifyPassDescriptorSet.Bind("_entityTiles", _entityTilesBuffer);
    resources.lightDescriptorSet.Bind("_entityTiles", _entityTilesBuffer);
}

void LightRenderer::SyncToGPU()
{
    if (_decals.SyncToGPU(_renderer))
    {
        RenderResources& resources = _gameRenderer->GetRenderResources();
        _classifyPassDescriptorSet.Bind("_packedDecals", _decals.GetBuffer());
        resources.lightDescriptorSet.Bind("_packedDecals", _decals.GetBuffer());
    }
}