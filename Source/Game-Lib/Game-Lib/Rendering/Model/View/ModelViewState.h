#pragma once

#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"
#include "Game-Lib/Rendering/Scene/StableRangeAllocator.h"
#include "ModelViewWork.h"
#include "Game-Lib/Rendering/Model/Scene/MeshletHistoryAllocator.h"

#include <Renderer/GPUVector.h>

#include <span>
#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace ModelLoading
{
    class ModelGeometryStorage;
}

namespace MaterialLoading
{
    class MaterialStorage;
}

namespace RenderScenes
{
    class RenderScene;
}

namespace ModelView
{
    // Owns CPU-side model selection and GPU-side LOD history inputs attached to one RenderView.
    // The inputs seed per-View GPU culling while history prevents unstable automatic LOD switching.
    class ModelViewState
    {
      public:
        explicit ModelViewState(bool validateTransfers = false);

        void SetDiagnosticSelection(RenderScenes::ModelInstanceHandle instance);
        void ClearDiagnosticSelection();
        void PrepareInputs(const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry);
        void PrepareChangedInputs(const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry, std::span<const u32> changedSlots);
        void PrepareTransparentStats(const RenderScenes::RenderScene& scene,
                                     const ModelLoading::ModelGeometryStorage& geometry,
                                     const MaterialLoading::MaterialStorage& materials);
        void PrepareChangedTransparentStats(const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry,
                                            const MaterialLoading::MaterialStorage& materials, std::span<const u32> changedSlots);
        void ResetLODHistory();
        void QueueTemporalClears(std::span<const u32> instanceSlots, std::span<const ModelScene::MeshletHistoryRange> meshletRanges);
        void AcknowledgeTemporalClears();
        bool SyncToGPU(Renderer::Renderer* renderer);

        bool IsWorkDirty() const { return _workDirty; }
        void MarkWorkDirty() { _workDirty = true; }
        void MarkWorkBuilt() { _workDirty = false; }
        std::span<const RenderScenes::ModelInstanceHandle> GetDiagnosticSelection() const { return _diagnosticSelection; }
        const Renderer::GPUVector<ViewInstanceInput>& GetInputs() const { return _inputs; }
        u32 GetDispatchInputCount() const { return _activeInputCount != 0 ? _inputs.Count() : 0; }
        u32 GetActiveInputCount() const { return _activeInputCount; }
        const Renderer::GPUVector<u32>& GetLODHistory() const { return _lodHistory; }
        std::span<const u32> GetPendingInstanceClears() const { return _pendingInstanceClears; }
        std::span<const ModelScene::MeshletHistoryRange> GetPendingMeshletClears() const
        {
            return _pendingMeshletClears;
        }
        u32 GetQueueCapacity() const { return _queueCapacity; }
        u32 GetLoadedLOD0Meshlets() const { return _loadedLOD0Meshlets; }
        u32 GetLoadedLOD0Triangles() const { return _loadedLOD0Triangles; }
        u32 GetLoadedLOD0TransparentMeshlets() const { return _loadedLOD0TransparentMeshlets; }
        u32 GetLoadedLOD0TransparentTriangles() const { return _loadedLOD0TransparentTriangles; }
        u64 GetPreparedTransparentRoutingRevision() const { return _preparedTransparentRoutingRevision; }
        u64 GetPreparedSceneRevision() const { return _preparedSceneRevision; }

      private:
        struct InputSlot
        {
            RenderScenes::ModelInstanceHandle handle = RenderScenes::InvalidModelInstanceHandle();
            RenderScenes::StableRange lodHistory;
            u32 meshlets = 0;
            u32 lod0Meshlets = 0;
            u32 lod0Triangles = 0;
            u32 transparentMeshlets = 0;
            u32 transparentTriangles = 0;
            bool active = false;
        };

        void DeactivateInputSlot(u32 slotIndex);
        void ActivateInputSlot(const RenderScenes::RenderScene& scene, u32 slotIndex, RenderScenes::ModelInstanceHandle handle,
                               const ModelLoading::ModelGeometryStorage& geometry);
        void EnsureInputSlot(u32 slotIndex);
        void RefreshTransparentStats(u32 slotIndex, const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry,
                                     const MaterialLoading::MaterialStorage& materials);

        std::vector<RenderScenes::ModelInstanceHandle> _diagnosticSelection;
        std::vector<RenderScenes::ModelInstanceHandle> _sceneSelection;
        std::vector<InputSlot> _inputSlots;
        RenderScenes::StableRangeAllocator _lodHistoryAllocator;
        std::vector<u32> _pendingInstanceClears;
        std::vector<ModelScene::MeshletHistoryRange> _pendingMeshletClears;
        Renderer::GPUVector<ViewInstanceInput> _inputs;
        Renderer::GPUVector<u32> _lodHistory;
        u32 _queueCapacity = 1;
        u32 _activeInputCount = 0;
        u32 _loadedLOD0Meshlets = 0;
        u32 _loadedLOD0Triangles = 0;
        u32 _loadedLOD0TransparentMeshlets = 0;
        u32 _loadedLOD0TransparentTriangles = 0;
        u64 _preparedTransparentRoutingRevision = 0;
        bool _workDirty = true;
        u64 _preparedSceneRevision = 0;
    };
} // namespace ModelView
