#pragma once
#include "Game-Lib/Rendering/Model/Scene/GeometryGroupMaskStore.h"
#include "Game-Lib/Rendering/Model/Scene/MeshletHistoryAllocator.h"
#include "Game-Lib/Rendering/Model/Scene/ModelInstanceStore.h"
#include "Game-Lib/Rendering/Model/Scene/ModelMaterialTableStore.h"
#include "Game-Lib/Rendering/Shadow/SceneShadowState.h"

#include <span>
#include <vector>

namespace MaterialLoading
{
    class MaterialStorage;
}

namespace ModelLoading
{
    class ModelGeometryStorage;
}

namespace Renderer
{
    class Renderer;
}

namespace Map
{
    struct ModelAllocationHints;
}

namespace RenderScenes
{
    struct ModelInstanceDesc
    {
        RenderAssets::ModelHandle model;
        mat4x4 worldTransform = mat4x4(1.0f);
        bool visible = true;
    };

    struct SceneClearRequests
    {
        std::span<const u32> instanceSlots;
        std::span<const ModelScene::MeshletHistoryRange> meshletHistoryRanges;

        bool IsEmpty() const { return instanceSlots.empty() && meshletHistoryRanges.empty(); }
    };

    struct RenderSceneStats
    {
        ModelScene::ModelInstanceStoreStats instances;
        ModelScene::ModelMaterialTableStoreStats materialTables;
        ModelScene::GeometryGroupMaskStoreStats geometryGroupMasks;
        ModelScene::MeshletHistoryAllocatorStats meshletHistory;
    };

    // Owns one renderer-wide Scene identity and composes its CPU-side model lifetime state with GPU-side instance data.
    // It keeps instance lifetime and GPU data coherent across every View of the Scene.
    class RenderScene
    {
      public:
        RenderScene(u64 sceneID, const ModelLoading::ModelGeometryStorage* geometryStorage,
                    const MaterialLoading::MaterialStorage* materialStorage, bool validateTransfers = false);

        void ReserveModelResources(const Map::ModelAllocationHints& hints);
        ModelInstanceHandle CreateModelInstance(const ModelInstanceDesc& desc);
        bool DestroyModelInstance(ModelInstanceHandle handle, u64 retireValue);
        bool SetModelTransform(ModelInstanceHandle handle, const mat4x4& transform, bool teleported = false);
        bool SetModelVisible(ModelInstanceHandle handle, bool visible);
        bool SetModelMaterial(ModelInstanceHandle handle, u32 slot, RenderAssets::MaterialInstanceHandle material);
        bool SetModelMaterials(ModelInstanceHandle handle,
                               std::span<const RenderAssets::MaterialInstanceHandle> materials);
        bool ResetModelMaterials(ModelInstanceHandle handle);
        bool SetGeometryGroupEnabled(ModelInstanceHandle handle, u32 groupID, bool enabled);
        bool SetAllGeometryGroups(ModelInstanceHandle handle, bool enabled);
        bool SetModelShadowDynamic(ModelInstanceHandle handle, bool dynamic) { return _instances.SetShadowDynamic(handle, dynamic); }

        u32 DrainShadowInvalidations(std::vector<vec4>& outMinMaxPairs, u32 maxPairs)
        {
            return _shadowState.DrainInvalidations(outMinMaxPairs, maxPairs);
        }
        std::span<const vec4> GetDynamicShadowAABBs() const { return _shadowState.GetDynamicAABBs(); }
        ShadowRendering::SceneShadowStats GetShadowStats() const { return _shadowState.GetStats(); }

        SceneClearRequests GetPendingClearRequests() const;
        void AcknowledgeClearsAndPublish();
        void ReleaseRetiredHistory(u64 completedValue);
        void AdvanceFrame();
        void SyncToGPU(Renderer::Renderer* renderer);

        u64 GetID() const { return _sceneID; }
        bool IsAlive(ModelInstanceHandle handle) const { return _instances.IsAlive(handle); }
        bool IsPending(ModelInstanceHandle handle) const { return _instances.IsPending(handle); }
        const ModelScene::ModelInstanceGPURecord* GetModelInstance(ModelInstanceHandle handle) const { return _instances.GetRecord(handle); }
        RenderSceneStats GetStats() const;

        const ModelScene::ModelInstanceStore& GetModelInstances() const { return _instances; }
        const ModelScene::ModelMaterialTableStore& GetModelMaterialTables() const { return _materialTables; }
        const ModelScene::GeometryGroupMaskStore& GetGeometryGroupMasks() const { return _geometryGroupMasks; }
        const ModelScene::MeshletHistoryAllocator& GetMeshletHistory() const { return _meshletHistory; }

      private:
        ModelMaterialTableHandle AcquireDefaultMaterialTable(RenderAssets::ModelHandle model) const;
        void RequestModelHistoryClear(ModelInstanceHandle handle);

        u64 _sceneID = 0;
        const ModelLoading::ModelGeometryStorage* _geometryStorage = nullptr;
        const MaterialLoading::MaterialStorage* _materialStorage = nullptr;
        mutable ModelScene::ModelMaterialTableStore _materialTables;
        ModelScene::GeometryGroupMaskStore _geometryGroupMasks;
        ModelScene::MeshletHistoryAllocator _meshletHistory;
        ModelScene::ModelInstanceStore _instances;
        ShadowRendering::SceneShadowState _shadowState;
    };
} // namespace RenderScenes
