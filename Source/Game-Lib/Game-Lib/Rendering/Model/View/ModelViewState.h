#pragma once

#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <Renderer/GPUVector.h>

#include <span>
#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace ModelView
{
    struct DiagnosticMeshletWork
    {
        u32 instanceIndex = 0;
        u32 meshletIndex = 0;
        u32 positionBase = 0;
        u32 vertexAttributeBase = 0;

        u32 meshletVertexIndexBase = 0;
        u32 meshletTriangleBase = 0;
        u32 colorSeed = 0;
        u32 reserved = 0;

        vec4 positionDecodeOffset = {};
        vec4 positionDecodeExtent = {};
    };
    static_assert(sizeof(DiagnosticMeshletWork) == 64);

    struct DiagnosticWorkStats
    {
        u32 selectedInstances = 0;
        u32 oneSidedMeshlets = 0;
        u32 twoSidedMeshlets = 0;
        u32 skippedInvisibleInstances = 0;
        u32 skippedSkinnedLODs = 0;
        u32 skippedGeometryGroups = 0;
    };

    // Owns CPU-side model selection and the GPU-side diagnostic meshlet queue attached to one RenderView.
    // It isolates temporary bring-up work from the general View and later production culling queues.
    class ModelViewState
    {
      public:
        explicit ModelViewState(bool validateTransfers = false);

        void SetDiagnosticSelection(RenderScenes::ModelInstanceHandle instance);
        void ClearDiagnosticSelection();
        void SetDiagnosticWork(std::span<const DiagnosticMeshletWork> oneSided,
                               std::span<const DiagnosticMeshletWork> twoSided, const DiagnosticWorkStats& stats);
        bool SyncToGPU(Renderer::Renderer* renderer);

        bool IsWorkDirty() const { return _workDirty; }
        void MarkWorkDirty() { _workDirty = true; }
        void MarkWorkBuilt() { _workDirty = false; }
        std::span<const RenderScenes::ModelInstanceHandle> GetDiagnosticSelection() const { return _diagnosticSelection; }
        const Renderer::GPUVector<DiagnosticMeshletWork>& GetDiagnosticWork() const { return _diagnosticWork; }
        u32 GetOneSidedCount() const { return _oneSidedCount; }
        u32 GetTwoSidedOffset() const { return _oneSidedCount; }
        u32 GetTwoSidedCount() const { return _twoSidedCount; }
        const DiagnosticWorkStats& GetDiagnosticStats() const { return _stats; }

      private:
        std::vector<RenderScenes::ModelInstanceHandle> _diagnosticSelection;
        Renderer::GPUVector<DiagnosticMeshletWork> _diagnosticWork;
        DiagnosticWorkStats _stats;
        u32 _oneSidedCount = 0;
        u32 _twoSidedCount = 0;
        bool _workDirty = true;
    };
} // namespace ModelView
