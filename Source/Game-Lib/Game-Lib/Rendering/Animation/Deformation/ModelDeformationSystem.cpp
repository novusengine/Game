#include "ModelDeformationSystem.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Animation/Debug/SkeletonDebugDraw.h"
#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace
{
    AutoCVar_Int CVAR_AnimationDeformationEnabled(CVarCategory::Client | CVarCategory::Rendering, "animationDeformationEnabled", "enable Animation V2 compute pre-skinning", 1, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDeformationQueueCapacity(CVarCategory::Client | CVarCategory::Rendering, "animationDeformationQueueCapacity", "hard Animation V2 deformation request capacity", 1024, CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDeformationDispatchCapacity(CVarCategory::Client | CVarCategory::Rendering, "animationDeformationDispatchCapacity", "hard Animation V2 expanded compute-dispatch capacity", 4096, CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationPaletteCapacity(CVarCategory::Client | CVarCategory::Rendering, "animationPaletteCapacity", "hard Animation V2 frame-linear palette matrix capacity", 65536, CVarFlags::DoNotSave);
    AutoCVar_Int CVAR_AnimationDeformationVertexCapacity(CVarCategory::Client | CVarCategory::Rendering, "animationDeformationVertexCapacity", "hard Animation V2 frame-linear deformed vertex capacity", 1048576, CVarFlags::DoNotSave);

    template <typename T>
    void Configure(Renderer::GPUVector<T>& buffer, const char* name)
    {
        buffer.SetDebugName(name);
        buffer.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    struct DeformationKey
    {
        u64 poseSource = 0;
        u64 poseGeneration = 0;
        u32 model = 0;
        u32 lod = 0;
        bool operator==(const DeformationKey&) const = default;
    };

    struct DeformationKeyHash
    {
        size_t operator()(const DeformationKey& key) const
        {
            size_t result = static_cast<size_t>(key.poseSource ^ (key.poseSource >> 32u));
            result ^= static_cast<size_t>(key.poseGeneration + 0x9E3779B97F4A7C15ull + (result << 6u) + (result >> 2u));
            result ^= static_cast<size_t>(key.model) * 0x9E3779B1u;
            result ^= static_cast<size_t>(key.lod) * 0x85EBCA77u;
            return result;
        }
    };
}

namespace Animation
{
    ModelDeformationSystem::ModelDeformationSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer, bool validateTransfers)
        : _renderer(renderer), _gameRenderer(gameRenderer), _descriptorSet(Renderer::DescriptorSetSlot::PER_PASS), _palettes(validateTransfers), _work(validateTransfers), _records(validateTransfers), _positions(validateTransfers), _vertexAttributes(validateTransfers)
    {
        _queueCapacity = static_cast<u32>(std::max(CVAR_AnimationDeformationQueueCapacity.Get(), 1));
        _dispatchCapacity = static_cast<u32>(std::max(CVAR_AnimationDeformationDispatchCapacity.Get(), 1));
        _paletteCapacity = static_cast<u32>(std::max(CVAR_AnimationPaletteCapacity.Get(), 1));
        _vertexCapacity = static_cast<u32>(std::max(CVAR_AnimationDeformationVertexCapacity.Get(), 1));
        Configure(_palettes, "Animation Frame Palettes");
        Configure(_work, "Animation Deformation Work");
        Configure(_records, "Animation Deformation Table");
        Configure(_positions, "Animation Deformed Positions");
        Configure(_vertexAttributes, "Animation Deformed Vertex Attributes");
        _palettes.Reserve(_paletteCapacity);
        _work.Reserve(_dispatchCapacity);
        _records.Reserve(_dispatchCapacity * 8u);
        _positions.Reserve(_vertexCapacity);
        _vertexAttributes.Reserve(_vertexCapacity);
        _positions.AddCount(_vertexCapacity);
        _vertexAttributes.AddCount(_vertexCapacity);
        _directPlayback = std::make_unique<DirectClipPlayer>(&_poseSources, &gameRenderer->GetRenderAssetResources()->GetAnimationRegistry());

        Renderer::ComputePipelineDesc desc;
        Renderer::ComputeShaderDesc shader;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Animation/ModelDeformation.cs"_h, "Animation/ModelDeformation.cs");
        desc.debugName = "Animation Model Deformation";
        desc.computeShader = renderer->LoadShader(shader);
        _pipeline = renderer->CreatePipeline(desc);
        _descriptorSet.RegisterPipeline(renderer, _pipeline);
        _descriptorSet.Init(renderer);
    }

    void ModelDeformationSystem::Prepare(RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry, const AnimationLoading::AnimationAssetRegistry& assets)
    {
        _stats = {};
        _palettes.Clear();
        _work.Clear();
        _records.Clear();
        scene.GetModelInstances().CollectActiveHandles(_instanceScratch);
        std::unordered_map<DeformationKey, ModelDeformationRecord, DeformationKeyHash> deformationCache;
        std::unordered_map<u64, u32> bindingTableOffsets;

        for (RenderScenes::ModelInstanceHandle instanceHandle : _instanceScratch)
        {
            const ModelScene::ModelInstanceResources* resources = scene.GetModelInstances().GetResources(instanceHandle);
            const ModelScene::ModelInstanceGPURecord* instance = scene.GetModelInstance(instanceHandle);
            if (!resources || !instance || !resources->poseSource)
                continue;
            if (!scene.GetModelInstances().IsVisible(instanceHandle))
            {
                scene.SetModelDeformationHandle(instanceHandle, RenderScenes::INVALID_SCENE_INDEX);
                continue;
            }
            if (_directPlayback->RequestPose(resources->poseSource))
                scene.MarkModelAnimated(instanceHandle);
            ++_stats.requests;
            if (CVAR_AnimationDeformationEnabled.Get() == 0 || _stats.requests > _queueCapacity)
            {
                if (_stats.requests > _queueCapacity)
                    ++_stats.queueOverflows;
                ++_stats.staticFallbacks;
                scene.SetModelDeformationHandle(instanceHandle, RenderScenes::INVALID_SCENE_INDEX);
                continue;
            }

            const PoseSource* pose = _poseSources.Get(resources->poseSource);
            if (!pose)
            {
                ++_stats.stalePoseSources;
                ++_stats.staticFallbacks;
                scene.SetModelDeformationHandle(instanceHandle, RenderScenes::INVALID_SCENE_INDEX);
                continue;
            }
            _gameRenderer->GetSkeletonDebugDraw()->SetCurrentLocalPose(pose->localPose);
            const FileFormat::AssetID skeletonAssetID = assets.GetAssetID(pose->skeleton);
            if (!geometry.HasModel(resources->model))
                continue;
            const ModelLoading::ModelGPURecord& model = geometry.GetRecord(resources->model);
            const u64 bindingKey = resources->poseSource.GetRawValue() ^ (static_cast<u64>(static_cast<RenderAssets::ModelHandle::type>(resources->model)) << 1u) ^ (pose->poseGeneration * 0x9E3779B97F4A7C15ull);
            auto tableIt = bindingTableOffsets.find(bindingKey);
            u32 tableBase = RenderScenes::INVALID_SCENE_INDEX;
            if (tableIt == bindingTableOffsets.end())
            {
                if (model.numMeshLODs > _dispatchCapacity * 8u - std::min(_records.Count(), _dispatchCapacity * 8u))
                {
                    ++_stats.dispatchOverflows;
                    ++_stats.staticFallbacks;
                    scene.SetModelDeformationHandle(instanceHandle, RenderScenes::INVALID_SCENE_INDEX);
                    continue;
                }
                tableBase = _records.AddCount(model.numMeshLODs);
                bindingTableOffsets.emplace(bindingKey, tableBase);
            }
            else
            {
                tableBase = tableIt->second;
            }

            bool compatible = true;
            for (u32 meshIndex = 0; meshIndex < model.numMeshes; ++meshIndex)
            {
                const FileFormat::Model::Mesh& mesh = geometry.GetMeshes()[model.meshBase + meshIndex];
                if (mesh.skeletonAssetID != skeletonAssetID)
                {
                    compatible = false;
                    continue;
                }
                if (mesh.numLODs == 0)
                    continue;
                const u32 absoluteLOD = model.meshLODBase + mesh.lodOffset;
                const FileFormat::Model::MeshLOD& lod = geometry.GetMeshLODs()[absoluteLOD];
                if ((lod.flags & FileFormat::Model::MeshLODFlags_HasSkinningData) == 0)
                    continue;
                const DeformationKey key = {resources->poseSource.GetRawValue(), pose->poseGeneration, static_cast<RenderAssets::ModelHandle::type>(resources->model), absoluteLOD};
                const auto cached = deformationCache.find(key);
                if (cached != deformationCache.end())
                {
                    _records[tableBase + mesh.lodOffset] = cached->second;
                    ++_stats.deduplicatedRequests;
                    continue;
                }
                if (_work.Count() >= _dispatchCapacity)
                {
                    ++_stats.dispatchOverflows;
                    continue;
                }
                if (lod.numVertices > _vertexCapacity - std::min(_stats.deformedVertices, _vertexCapacity))
                {
                    ++_stats.deformationArenaOverflows;
                    continue;
                }
                if (lod.numJointPaletteRemaps > _paletteCapacity - std::min(_palettes.Count(), _paletteCapacity))
                {
                    ++_stats.paletteOverflows;
                    continue;
                }

                const u32 paletteOffset = _palettes.Count();
                for (u32 remapIndex = 0; remapIndex < lod.numJointPaletteRemaps; ++remapIndex)
                {
                    const u32 skeletonJoint = geometry.GetJointPaletteRemaps()[model.jointPaletteRemapBase + lod.jointPaletteRemapOffset + remapIndex];
                    if (skeletonJoint >= pose->palette.size())
                    {
                        compatible = false;
                        break;
                    }
                    _palettes.Add(pose->palette[skeletonJoint]);
                }
                if (!compatible)
                    break;

                const u32 outputOffset = _stats.deformedVertices;
                ModelDeformationWork work;
                work.positionDecodeOffset = vec4(mesh.positionDecodeOffset, 0.0f);
                work.positionDecodeExtent = vec4(mesh.positionDecodeExtent, 0.0f);
                work.sourcePositionOffset = model.positionBase + lod.vertexOffset;
                work.sourceVertexAttributeOffset = model.vertexAttributeBase + lod.vertexAttributeOffset;
                work.sourceSkinningOffset = model.skinningDataBase + lod.skinningDataOffset;
                work.vertexCount = lod.numVertices;
                work.paletteOffset = paletteOffset;
                work.paletteCount = lod.numJointPaletteRemaps;
                work.outputPositionOffset = outputOffset;
                work.outputVertexAttributeOffset = outputOffset;
                _work.Add(work);

                const ModelDeformationRecord record = {outputOffset, outputOffset, lod.numVertices, static_cast<u32>(pose->poseGeneration)};
                _records[tableBase + mesh.lodOffset] = record;
                deformationCache.emplace(key, record);
                _stats.deformedVertices += lod.numVertices;
                ++_stats.dispatches;
            }

            if (!compatible)
            {
                ++_stats.incompatibleSkeletons;
                ++_stats.staticFallbacks;
                scene.SetModelDeformationHandle(instanceHandle, RenderScenes::INVALID_SCENE_INDEX);
                continue;
            }
            scene.SetModelDeformationHandle(instanceHandle, tableBase);
        }
        _stats.poseSources = _poseSources.GetStats().liveSources;
        _stats.paletteMatrices = _palettes.Count();
        if (_records.Count() != 0)
            _records.SetDirty();
    }

    void ModelDeformationSystem::Upload()
    {
        _palettes.SyncToGPU(_renderer);
        _work.SyncToGPU(_renderer);
        _records.SyncToGPU(_renderer);
        _positions.SyncToGPU(_renderer);
        _vertexAttributes.SyncToGPU(_renderer);
        _descriptorSet.Bind("_animationSkinningWork"_h, _work.GetBuffer());
        _descriptorSet.Bind("_animationSkinningPalettes"_h, _palettes.GetBuffer());
        _descriptorSet.Bind("_animationDeformedPositions"_h, _positions.GetBuffer());
        _descriptorSet.Bind("_animationDeformedVertexAttributes"_h, _vertexAttributes.GetBuffer());
    }

    void ModelDeformationSystem::AddPass(Renderer::RenderGraph* renderGraph, const ModelLoading::ModelGeometryStorage& geometry)
    {
        if (_work.Count() == 0)
            return;
        _descriptorSet.Bind("_animationSourcePositions"_h, geometry.GetPositions().GetBuffer());
        _descriptorSet.Bind("_animationSourceVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer());
        _descriptorSet.Bind("_animationSourceSkinning"_h, geometry.GetSkinningData().GetBuffer());
        struct Data { Renderer::DescriptorSetResource set; };
        renderGraph->AddPass<Data>("Animation Model Deformation",
            [this, &geometry](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                builder.Read(geometry.GetPositions().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetVertexAttributes().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetSkinningData().GetBuffer(), Usage::COMPUTE);
                builder.Read(_work.GetBuffer(), Usage::COMPUTE);
                builder.Read(_palettes.GetBuffer(), Usage::COMPUTE);
                builder.Write(_positions.GetBuffer(), Usage::COMPUTE);
                builder.Write(_vertexAttributes.GetBuffer(), Usage::COMPUTE);
                data.set = builder.Use(_descriptorSet);
                return true;
            },
            [this, &geometry](Data& data, Renderer::RenderGraphResources&, Renderer::CommandList& commandList) {
                commandList.BeginPipeline(_pipeline);
                commandList.BindDescriptorSet(data.set, 0);
                struct Constants { u32 workCount; u32 reserved0; u32 reserved1; u32 reserved2; } constants = {_work.Count(), 0, 0, 0};
                commandList.PushConstant(&constants, 0, sizeof(constants));
                commandList.Dispatch(_work.Count(), 1, 1);
                commandList.EndPipeline(_pipeline);
            }, Renderer::RenderPassFlags::None);
    }
} // namespace Animation
