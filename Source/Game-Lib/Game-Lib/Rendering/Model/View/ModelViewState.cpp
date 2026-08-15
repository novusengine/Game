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
        _inputs.Clear();
        _lodHistory.Clear();
        _queueCapacity = 1;
        _loadedLOD0Meshlets = 0;
        _loadedLOD0Triangles = 0;

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
            {
                _lodHistory[historyBase + meshIndex] = INVALID_LOD_HISTORY;
                const FileFormat::Model::Mesh& mesh = geometry.GetMeshes()[modelRecord.meshBase + meshIndex];
                const FileFormat::Model::MeshLOD& lod = geometry.GetMeshLODs()[modelRecord.meshLODBase + mesh.lodOffset];
                _loadedLOD0Meshlets += lod.numMeshlets;
                for (u32 meshletIndex = 0; meshletIndex < lod.numMeshlets; ++meshletIndex)
                    _loadedLOD0Triangles += geometry.GetMeshlets()[modelRecord.meshletBase + lod.meshletOffset + meshletIndex].triangleCount;
            }
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

    void ModelViewState::PrepareTransparentStats(const RenderScenes::RenderScene& scene,
                                                 const ModelLoading::ModelGeometryStorage& geometry,
                                                 const MaterialLoading::MaterialStorage& materials)
    {
        _loadedLOD0TransparentMeshlets = 0;
        _loadedLOD0TransparentTriangles = 0;
        const std::span<const RenderScenes::ModelInstanceHandle> selection = [&]() {
            if (!_diagnosticSelection.empty())
                return std::span<const RenderScenes::ModelInstanceHandle>(_diagnosticSelection);
            scene.GetModelInstances().CollectActiveHandles(_sceneSelection);
            return std::span<const RenderScenes::ModelInstanceHandle>(_sceneSelection);
        }();

        const auto& materialTable = scene.GetModelMaterialTables().GetEntries();
        for (const RenderScenes::ModelInstanceHandle handle : selection)
        {
            const ModelScene::ModelInstanceGPURecord* instance = scene.GetModelInstance(handle);
            if (!scene.IsAlive(handle) || !instance)
                continue;

            const RenderAssets::ModelHandle model(instance->modelIndex);
            if (!geometry.HasModel(model))
                continue;

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
                    const u32 slot = mesh.materialSlotOffset + submesh.materialSlotIndex;
                    if (slot >= instance->materialTableCount)
                        continue;
                    const u32 materialInstanceIndex = materialTable[instance->materialTableOffset + slot];
                    if (materialInstanceIndex >= materials.GetMaterialInstances().Count())
                        continue;
                    const MaterialLoading::MaterialInstanceGPURecord& materialInstance = materials.GetMaterialInstances()[materialInstanceIndex];
                    const u32 executionGroupClass = (materialInstance.packedClassification >> 16u) % FileFormat::Material::ABI::EXECUTION_GROUP_CLASS_COUNT;
                    const bool transparent = executionGroupClass / 2u == static_cast<u32>(FileFormat::Material::RasterClass::Transparent);
                    if (forceTransparent || transparent)
                    {
                        _loadedLOD0TransparentMeshlets += submesh.numMeshlets;
                        for (u32 meshletIndex = 0; meshletIndex < submesh.numMeshlets; ++meshletIndex)
                            _loadedLOD0TransparentTriangles += geometry.GetMeshlets()[modelRecord.meshletBase + submesh.meshletOffset + meshletIndex].triangleCount;
                    }
                }
            }
        }
        _preparedTransparentRoutingRevision = scene.GetTransparentRoutingRevision();
    }

    bool ModelViewState::SyncToGPU(Renderer::Renderer* renderer)
    {
        const bool inputsChanged = _inputs.SyncToGPU(renderer);
        const bool historyChanged = _lodHistory.SyncToGPU(renderer);
        return inputsChanged || historyChanged;
    }
} // namespace ModelView
