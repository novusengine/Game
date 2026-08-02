#pragma once

#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"
#include "ModelViewWork.h"

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
        void ResetLODHistory();
        bool SyncToGPU(Renderer::Renderer* renderer);

        bool IsWorkDirty() const { return _workDirty; }
        void MarkWorkDirty() { _workDirty = true; }
        void MarkWorkBuilt() { _workDirty = false; }
        std::span<const RenderScenes::ModelInstanceHandle> GetDiagnosticSelection() const { return _diagnosticSelection; }
        const Renderer::GPUVector<ViewInstanceInput>& GetInputs() const { return _inputs; }
        const Renderer::GPUVector<u32>& GetLODHistory() const { return _lodHistory; }
        u32 GetQueueCapacity() const { return _queueCapacity; }

      private:
        std::vector<RenderScenes::ModelInstanceHandle> _diagnosticSelection;
        Renderer::GPUVector<ViewInstanceInput> _inputs;
        Renderer::GPUVector<u32> _lodHistory;
        u32 _queueCapacity = 1;
        bool _workDirty = true;
    };
} // namespace ModelView
