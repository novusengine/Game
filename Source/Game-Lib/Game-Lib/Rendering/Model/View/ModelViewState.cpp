#include "ModelViewState.h"

#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Renderer/Renderer.h>

namespace ModelView
{
    ModelViewState::ModelViewState(bool validateTransfers)
        : _inputs(validateTransfers), _lodHistory(validateTransfers)
    {
        _inputs.SetDebugName("Model View Instance Inputs");
        _inputs.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _lodHistory.SetDebugName("Model View LOD History");
        _lodHistory.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
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

    void ModelViewState::PrepareInputs(const RenderScenes::RenderScene& scene,
                                       const ModelLoading::ModelGeometryStorage& geometry)
    {
        _inputs.Clear();
        _lodHistory.Clear();
        _queueCapacity = 1;

        const std::span<const RenderScenes::ModelInstanceHandle> selection = [&]() {
            if (!_diagnosticSelection.empty())
                return std::span<const RenderScenes::ModelInstanceHandle>(_diagnosticSelection);
            scene.GetModelInstances().CollectActiveHandles(_sceneSelection);
            return std::span<const RenderScenes::ModelInstanceHandle>(_sceneSelection);
        }();

        for (const RenderScenes::ModelInstanceHandle handle : selection)
        {
            const ModelScene::ModelInstanceGPURecord* instance = scene.GetModelInstance(handle);
            if (!scene.IsAlive(handle) || !instance)
                continue;

            const RenderAssets::ModelHandle model(instance->modelIndex);
            if (!geometry.HasModel(model))
                continue;

            const ModelLoading::ModelGPURecord& modelRecord = geometry.GetRecord(model);
            ViewInstanceInput input;
            input.instanceIndex = RenderScenes::GetModelInstanceSlot(handle);
            input.lodHistoryOffset = _lodHistory.Count();
            _inputs.Add(input);

            const u32 historyBase = _lodHistory.AddCount(modelRecord.numMeshes);
            for (u32 meshIndex = 0; meshIndex < modelRecord.numMeshes; ++meshIndex)
                _lodHistory[historyBase + meshIndex] = INVALID_LOD_HISTORY;
            _queueCapacity += modelRecord.numMeshlets;
        }

        _preparedSceneRevision = scene.GetModelInstances().GetMembershipRevision();
        _workDirty = false;
    }

    void ModelViewState::ResetLODHistory()
    {
        for (u32 index = 0; index < _lodHistory.Count(); ++index)
            _lodHistory[index] = INVALID_LOD_HISTORY;
        _lodHistory.SetDirty();
    }

    bool ModelViewState::SyncToGPU(Renderer::Renderer* renderer)
    {
        const bool inputsChanged = _inputs.SyncToGPU(renderer);
        const bool historyChanged = _lodHistory.SyncToGPU(renderer);
        return inputsChanged || historyChanged;
    }
} // namespace ModelView
