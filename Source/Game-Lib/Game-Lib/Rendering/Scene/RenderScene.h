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
    struct ModelRenderDescription;

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
        bool DestroyModelInstance(ModelInstanceHandle handle);
        bool SetModelTransform(ModelInstanceHandle handle, const mat4x4& transform, bool teleported = false);
        bool SetModelVisible(ModelInstanceHandle handle, bool visible);
        bool SetModelHighlight(ModelInstanceHandle handle, f32 intensity, u32 packedColor = 0xFFFFFFFFu);
        bool SetModelOpacity(ModelInstanceHandle handle, f32 opacity, bool forceTransparent = false);
        bool SetModelCastsShadows(ModelInstanceHandle handle, bool castsShadows);
        bool SetModelMaterial(ModelInstanceHandle handle, u32 slot, RenderAssets::MaterialInstanceHandle material);
        bool SetModelMaterials(ModelInstanceHandle handle,
                               std::span<const RenderAssets::MaterialInstanceHandle> materials);
        bool ResetModelMaterials(ModelInstanceHandle handle);
        bool SetGeometryGroupEnabled(ModelInstanceHandle handle, u32 groupID, bool enabled);
        bool SetGeometryGroupRangeEnabled(ModelInstanceHandle handle, u32 firstGroupID, u32 lastGroupID, bool enabled);
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
        std::span<const u32> GetModelMembershipChanges() const { return _instances.GetMembershipChanges(); }
        void AcknowledgeModelMembershipChanges() { _instances.AcknowledgeMembershipChanges(); }
        std::span<const u32> GetTransparentRoutingChanges() const { return _instances.GetRoutingChanges(); }
        void AcknowledgeTransparentRoutingChanges() { _instances.AcknowledgeRoutingChanges(); }
        void ReleaseRetiredHistory(u64 completedValue);
        void FlushModelResourceFrees();
        void SetHistoryRetireValue(u64 submissionValue) { _historyRetireValue = submissionValue; }
        void AdvanceFrame();
        void SyncToGPU(Renderer::Renderer* renderer);

        u64 GetID() const { return _sceneID; }
        bool IsAlive(ModelInstanceHandle handle) const { return _instances.IsAlive(handle); }
        bool IsPending(ModelInstanceHandle handle) const { return _instances.IsPending(handle); }
        const ModelScene::ModelInstanceGPURecord* GetModelInstance(ModelInstanceHandle handle) const { return _instances.GetRecord(handle); }
        bool DescribeModelInstance(ModelInstanceHandle handle, ModelRenderDescription& description) const;
        RenderSceneStats GetStats() const;
        u64 GetTransparentRoutingRevision() const { return _transparentRoutingRevision; }

        const ModelScene::ModelInstanceStore& GetModelInstances() const { return _instances; }
        bool HasModelHighlights() const { return _instances.GetHighlightedInstanceCount() != 0; }
        bool HasTransparentModelHighlights() const;
        const ModelScene::ModelMaterialTableStore& GetModelMaterialTables() const { return _materialTables; }
        const ModelScene::GeometryGroupMaskStore& GetGeometryGroupMasks() const { return _geometryGroupMasks; }
        const ModelScene::MeshletHistoryAllocator& GetMeshletHistory() const { return _meshletHistory; }

      private:
        ModelMaterialTableHandle AcquireDefaultMaterialTable(RenderAssets::ModelHandle model) const;
        void RequestModelHistoryClear(ModelInstanceHandle handle);

        u64 _sceneID = 0;
        u64 _historyRetireValue = 0;
        const ModelLoading::ModelGeometryStorage* _geometryStorage = nullptr;
        const MaterialLoading::MaterialStorage* _materialStorage = nullptr;
        mutable ModelScene::ModelMaterialTableStore _materialTables;
        mutable std::vector<ModelMaterialTableHandle> _defaultMaterialTables;
        ModelScene::GeometryGroupMaskStore _geometryGroupMasks;
        ModelScene::MeshletHistoryAllocator _meshletHistory;
        ModelScene::ModelInstanceStore _instances;
        ShadowRendering::SceneShadowState _shadowState;
        mutable std::vector<ModelInstanceHandle> _highlightedModelScratch;
        mutable bool _transparentHighlightsDirty = true;
        mutable bool _hasTransparentHighlights = false;
        u64 _transparentRoutingRevision = 0;
    };
} // namespace RenderScenes
