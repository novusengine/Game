#include "ModelViewState.h"

#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
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
        const std::span<const RenderScenes::ModelInstanceHandle> selection = [&]() {
            if (!_diagnosticSelection.empty())
                return std::span<const RenderScenes::ModelInstanceHandle>(_diagnosticSelection);
            scene.GetModelInstances().CollectActiveHandles(_sceneSelection);
            return std::span<const RenderScenes::ModelInstanceHandle>(_sceneSelection);
        }();

        std::vector<bool> desired(_inputSlots.size(), false);

        for (const RenderScenes::ModelInstanceHandle handle : selection)
        {
            const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
            EnsureInputSlot(slotIndex);
            desired.resize(_inputSlots.size(), false);
            desired[slotIndex] = true;
        }

        for (u32 slotIndex = 0; slotIndex < _inputSlots.size(); ++slotIndex)
        {
            const RenderScenes::ModelInstanceHandle current = slotIndex < _inputSlots.size() && desired[slotIndex]
                ? scene.GetModelInstances().GetHandleAtSlot(slotIndex)
                : RenderScenes::InvalidModelInstanceHandle();
            if (_inputSlots[slotIndex].active && (!desired[slotIndex] || _inputSlots[slotIndex].handle != current))
                DeactivateInputSlot(slotIndex);
        }

        _lodHistoryAllocator.FlushFrees();
        for (const RenderScenes::ModelInstanceHandle handle : selection)
        {
            const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
            if (!_inputSlots[slotIndex].active)
                ActivateInputSlot(scene, slotIndex, handle, geometry);
        }

        _preparedSceneRevision = scene.GetModelInstances().GetMembershipRevision();
        _workDirty = false;
    }

    void ModelViewState::PrepareChangedInputs(const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry, std::span<const u32> changedSlots)
    {
        for (const u32 slotIndex : changedSlots)
        {
            if (slotIndex >= _inputSlots.size())
                continue;
            const RenderScenes::ModelInstanceHandle handle = scene.GetModelInstances().GetHandleAtSlot(slotIndex);
            if (_inputSlots[slotIndex].active && _inputSlots[slotIndex].handle != handle)
                DeactivateInputSlot(slotIndex);
        }
        _lodHistoryAllocator.FlushFrees();

        for (const u32 slotIndex : changedSlots)
        {
            const RenderScenes::ModelInstanceHandle handle = scene.GetModelInstances().GetHandleAtSlot(slotIndex);
            if (handle == RenderScenes::InvalidModelInstanceHandle() || !scene.IsAlive(handle))
                continue;
            EnsureInputSlot(slotIndex);
            if (!_inputSlots[slotIndex].active)
                ActivateInputSlot(scene, slotIndex, handle, geometry);
        }

        _preparedSceneRevision = scene.GetModelInstances().GetMembershipRevision();
        _workDirty = false;
    }

    void ModelViewState::EnsureInputSlot(u32 slotIndex)
    {
        if (slotIndex < _inputSlots.size())
            return;
        const size_t oldSize = _inputSlots.size();
        _inputSlots.resize(static_cast<size_t>(slotIndex) + 1u);
        const u32 firstInput = _inputs.AddCount(static_cast<u32>(_inputSlots.size() - oldSize));
        for (u32 inputIndex = firstInput; inputIndex < _inputs.Count(); ++inputIndex)
            _inputs[inputIndex].instanceIndex = RenderScenes::INVALID_SCENE_INDEX;
    }

    void ModelViewState::ActivateInputSlot(const RenderScenes::RenderScene& scene, u32 slotIndex, RenderScenes::ModelInstanceHandle handle,
                                           const ModelLoading::ModelGeometryStorage& geometry)
    {
        const ModelScene::ModelInstanceGPURecord* instance = scene.GetModelInstance(handle);
        if (!scene.IsAlive(handle) || !instance)
            return;

        const RenderAssets::ModelHandle model(instance->modelIndex);
        if (!geometry.HasModel(model))
            return;
        const ModelLoading::ModelGPURecord& modelRecord = geometry.GetRecord(model);
        const RenderScenes::StableRange history = _lodHistoryAllocator.Allocate(modelRecord.numMeshes);
        const u32 previousHistoryCount = _lodHistory.Count();
        if (history.offset + history.count > previousHistoryCount)
            _lodHistory.AddCount(history.offset + history.count - previousHistoryCount);
        for (u32 meshIndex = 0; meshIndex < modelRecord.numMeshes; ++meshIndex)
            _lodHistory[history.offset + meshIndex] = INVALID_LOD_HISTORY;
        if (history.count != 0 && history.offset < previousHistoryCount)
            _lodHistory.SetDirtyElements(history.offset, history.count);

        InputSlot& slot = _inputSlots[slotIndex];
        slot.handle = handle;
        slot.lodHistory = history;
        slot.meshlets = modelRecord.numMeshlets;
        slot.lod0Meshlets = modelRecord.lod0Meshlets;
        slot.lod0Triangles = modelRecord.lod0Triangles;
        slot.active = true;
        ++_activeInputCount;
        _queueCapacity += slot.meshlets;
        _loadedLOD0Meshlets += slot.lod0Meshlets;
        _loadedLOD0Triangles += slot.lod0Triangles;
        _inputs[slotIndex] = {slotIndex, history.offset};
        _inputs.SetDirtyElement(slotIndex);
    }

    void ModelViewState::DeactivateInputSlot(u32 slotIndex)
    {
        InputSlot& slot = _inputSlots[slotIndex];
        _queueCapacity -= slot.meshlets;
        --_activeInputCount;
        _loadedLOD0Meshlets -= slot.lod0Meshlets;
        _loadedLOD0Triangles -= slot.lod0Triangles;
        _loadedLOD0TransparentMeshlets -= slot.transparentMeshlets;
        _loadedLOD0TransparentTriangles -= slot.transparentTriangles;
        _lodHistoryAllocator.Free(slot.lodHistory);
        slot = {};
        _inputs[slotIndex] = {RenderScenes::INVALID_SCENE_INDEX, 0};
        _inputs.SetDirtyElement(slotIndex);
    }

    void ModelViewState::ResetLODHistory()
    {
        for (u32 index = 0; index < _lodHistory.Count(); ++index)
            _lodHistory[index] = INVALID_LOD_HISTORY;
        _lodHistory.SetDirty();
    }

    void ModelViewState::QueueTemporalClears(std::span<const u32> instanceSlots, std::span<const ModelScene::MeshletHistoryRange> meshletRanges)
    {
        _pendingInstanceClears.insert(_pendingInstanceClears.end(), instanceSlots.begin(), instanceSlots.end());
        _pendingMeshletClears.insert(_pendingMeshletClears.end(), meshletRanges.begin(), meshletRanges.end());
    }

    void ModelViewState::AcknowledgeTemporalClears()
    {
        _pendingInstanceClears.clear();
        _pendingMeshletClears.clear();
    }

    void ModelViewState::PrepareTransparentStats(const RenderScenes::RenderScene& scene,
                                                 const ModelLoading::ModelGeometryStorage& geometry,
                                                 const MaterialLoading::MaterialStorage& materials)
    {
        _loadedLOD0TransparentMeshlets = 0;
        _loadedLOD0TransparentTriangles = 0;
        for (InputSlot& slot : _inputSlots)
        {
            slot.transparentMeshlets = 0;
            slot.transparentTriangles = 0;
        }
        for (u32 slotIndex = 0; slotIndex < _inputSlots.size(); ++slotIndex)
            RefreshTransparentStats(slotIndex, scene, geometry, materials);
        _preparedTransparentRoutingRevision = scene.GetTransparentRoutingRevision();
    }

    void ModelViewState::PrepareChangedTransparentStats(const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry,
                                                        const MaterialLoading::MaterialStorage& materials, std::span<const u32> changedSlots)
    {
        for (const u32 slotIndex : changedSlots)
            if (slotIndex < _inputSlots.size())
                RefreshTransparentStats(slotIndex, scene, geometry, materials);
        _preparedTransparentRoutingRevision = scene.GetTransparentRoutingRevision();
    }

    void ModelViewState::RefreshTransparentStats(u32 slotIndex, const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry,
                                                 const MaterialLoading::MaterialStorage& materials)
    {
        InputSlot& slotState = _inputSlots[slotIndex];
        _loadedLOD0TransparentMeshlets -= slotState.transparentMeshlets;
        _loadedLOD0TransparentTriangles -= slotState.transparentTriangles;
        slotState.transparentMeshlets = 0;
        slotState.transparentTriangles = 0;
        if (!slotState.active)
            return;

        const ModelScene::ModelInstanceGPURecord* instance = scene.GetModelInstance(slotState.handle);
        if (!instance)
            return;
        const RenderAssets::ModelHandle model(instance->modelIndex);
        if (!geometry.HasModel(model))
            return;

        const auto& materialTable = scene.GetModelMaterialTables().GetEntries();
        const bool forceTransparent = (instance->flags & ModelScene::ModelInstanceFlagForceTransparent) != 0;
        const ModelLoading::ModelGPURecord& modelRecord = geometry.GetRecord(model);
        for (u32 meshIndex = 0; meshIndex < modelRecord.numMeshes; ++meshIndex)
        {
            const FileFormat::Model::Mesh& mesh = geometry.GetMeshes()[modelRecord.meshBase + meshIndex];
            if (mesh.numLODs == 0)
                continue;
            const FileFormat::Model::MeshLOD& lod = geometry.GetMeshLODs()[modelRecord.meshLODBase + mesh.lodOffset];
            for (u32 submeshIndex = 0; submeshIndex < lod.numSubmeshes; ++submeshIndex)
            {
                const FileFormat::Model::Submesh& submesh = geometry.GetSubmeshes()[modelRecord.submeshBase + lod.submeshOffset + submeshIndex];
                const u32 materialSlot = mesh.materialSlotOffset + submesh.materialSlotIndex;
                if (materialSlot >= instance->materialTableCount)
                    continue;
                const u32 materialInstanceIndex = materialTable[instance->materialTableOffset + materialSlot];
                if (materialInstanceIndex >= materials.GetMaterialInstances().Count())
                    continue;
                const auto& materialInstance = materials.GetMaterialInstances()[materialInstanceIndex];
                const u32 executionGroupClass = (materialInstance.packedClassification >> 16u) % FileFormat::Material::ABI::EXECUTION_GROUP_CLASS_COUNT;
                const bool transparent = executionGroupClass / 2u == static_cast<u32>(FileFormat::Material::RasterClass::Transparent);
                if (!forceTransparent && !transparent)
                    continue;
                slotState.transparentMeshlets += submesh.numMeshlets;
                for (u32 meshletIndex = 0; meshletIndex < submesh.numMeshlets; ++meshletIndex)
                    slotState.transparentTriangles += geometry.GetMeshlets()[modelRecord.meshletBase + submesh.meshletOffset + meshletIndex].triangleCount;
            }
        }
        _loadedLOD0TransparentMeshlets += slotState.transparentMeshlets;
        _loadedLOD0TransparentTriangles += slotState.transparentTriangles;
    }

    bool ModelViewState::SyncToGPU(Renderer::Renderer* renderer)
    {
        const bool inputsChanged = _inputs.SyncToGPU(renderer);
        const bool historyChanged = _lodHistory.SyncToGPU(renderer);
        return inputsChanged || historyChanged;
    }
} // namespace ModelView
