#include "ModelViewState.h"

#include <Renderer/Renderer.h>

namespace ModelView
{
    ModelViewState::ModelViewState(bool validateTransfers)
        : _diagnosticWork(validateTransfers)
    {
        _diagnosticWork.SetDebugName("Model Diagnostic Meshlet Work");
        _diagnosticWork.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    void ModelViewState::SetDiagnosticSelection(RenderScenes::ModelInstanceHandle instance)
    {
        _diagnosticSelection.assign(1, instance);
        _workDirty = true;
    }

    void ModelViewState::ClearDiagnosticSelection()
    {
        _diagnosticSelection.clear();
        _workDirty = true;
    }

    void ModelViewState::SetDiagnosticWork(std::span<const DiagnosticMeshletWork> oneSided,
                                           std::span<const DiagnosticMeshletWork> twoSided,
                                           const DiagnosticWorkStats& stats)
    {
        _diagnosticWork.Clear();
        _oneSidedCount = static_cast<u32>(oneSided.size());
        _twoSidedCount = static_cast<u32>(twoSided.size());

        const u32 workBase = _diagnosticWork.AddCount(_oneSidedCount + _twoSidedCount);
        for (u32 index = 0; index < _oneSidedCount; ++index)
            _diagnosticWork[workBase + index] = oneSided[index];
        for (u32 index = 0; index < _twoSidedCount; ++index)
            _diagnosticWork[workBase + _oneSidedCount + index] = twoSided[index];

        _stats = stats;
        _workDirty = false;
    }

    bool ModelViewState::SyncToGPU(Renderer::Renderer* renderer)
    {
        return _diagnosticWork.SyncToGPU(renderer);
    }
} // namespace ModelView
