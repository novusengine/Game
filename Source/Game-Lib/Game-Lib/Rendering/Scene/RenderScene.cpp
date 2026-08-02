#include "RenderScene.h"

#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"

#include <vector>

namespace RenderScenes
{
    RenderScene::RenderScene(u64 sceneID, const ModelLoading::ModelGeometryStorage* geometryStorage,
                             const MaterialLoading::MaterialStorage* materialStorage, bool validateTransfers)
        : _sceneID(sceneID), _geometryStorage(geometryStorage), _materialStorage(materialStorage),
          _materialTables(validateTransfers), _geometryGroupMasks(validateTransfers), _instances(validateTransfers)
    {
    }

    ModelInstanceHandle RenderScene::CreateModelInstance(const ModelInstanceDesc& desc)
    {
        if (!_geometryStorage || !_materialStorage || !_geometryStorage->HasModel(desc.model))
            return InvalidModelInstanceHandle();

        const ModelLoading::ModelGPURecord& model = _geometryStorage->GetRecord(desc.model);
        const ModelMaterialTableHandle materialTable = AcquireDefaultMaterialTable(desc.model);
        if (!_materialTables.IsValid(materialTable))
            return InvalidModelInstanceHandle();

        const GeometryGroupMaskHandle groupMask = _geometryGroupMasks.Create(model.geometryGroupCount);
        const ModelScene::MeshletHistoryRange history = _meshletHistory.Allocate((model.numMeshlets + 31u) / 32u);

        ModelScene::ModelInstanceCreateInfo createInfo;
        createInfo.model = desc.model;
        createInfo.worldTransform = desc.worldTransform;
        createInfo.materialTable = materialTable;
        createInfo.materialTableOffset = _materialTables.GetOffset(materialTable);
        createInfo.materialTableCount = _materialTables.GetCount(materialTable);
        createInfo.geometryGroupMask = groupMask;
        createInfo.geometryGroupWordOffset = _geometryGroupMasks.GetOffset(groupMask);
        createInfo.geometryGroupWordCount = _geometryGroupMasks.GetWordCount(groupMask);
        createInfo.meshletHistory = history;
        createInfo.visible = desc.visible;
        return _instances.Create(createInfo);
    }

    bool RenderScene::DestroyModelInstance(ModelInstanceHandle handle, u64 retireValue)
    {
        ModelScene::ModelInstanceResources resources;
        if (!_instances.Destroy(handle, resources))
            return false;

        _materialTables.Release(resources.materialTable);
        _geometryGroupMasks.Release(resources.geometryGroupMask);
        _meshletHistory.Retire(resources.meshletHistory, retireValue);
        return true;
    }

    bool RenderScene::SetModelTransform(ModelInstanceHandle handle, const mat4x4& transform, bool teleported)
    {
        bool needsHistoryClear = false;
        if (!_instances.SetTransform(handle, transform, teleported, needsHistoryClear))
            return false;

        if (needsHistoryClear)
        {
            const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
            _meshletHistory.RequestClear(resources->meshletHistory);
        }
        return true;
    }

    bool RenderScene::SetModelVisible(ModelInstanceHandle handle, bool visible)
    {
        bool needsHistoryClear = false;
        if (!_instances.SetVisible(handle, visible, needsHistoryClear))
            return false;

        if (needsHistoryClear)
        {
            const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
            _meshletHistory.RequestClear(resources->meshletHistory);
        }
        return true;
    }

    bool RenderScene::SetModelMaterial(ModelInstanceHandle handle, u32 slot, RenderAssets::MaterialInstanceHandle material)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!resources)
            return false;

        ModelMaterialTableHandle table = resources->materialTable;
        if (slot >= _materialTables.GetCount(table))
            return false;

        if (!_materialTables.IsPrivate(table))
        {
            const ModelMaterialTableHandle privateTable = _materialTables.CreatePrivate(table);
            if (!_materialTables.IsValid(privateTable))
                return false;

            if (!_instances.SetMaterialTable(handle, privateTable, _materialTables.GetOffset(privateTable),
                                             _materialTables.GetCount(privateTable), true))
            {
                _materialTables.Release(privateTable);
                return false;
            }
            _materialTables.Release(table);
            table = privateTable;
        }

        return _materialTables.SetMaterial(table, slot, material);
    }

    bool RenderScene::ResetModelMaterials(ModelInstanceHandle handle)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!resources || !_materialTables.IsPrivate(resources->materialTable))
            return resources != nullptr;

        const ModelMaterialTableHandle oldTable = resources->materialTable;
        const ModelMaterialTableHandle sharedTable = AcquireDefaultMaterialTable(resources->model);
        if (!_materialTables.IsValid(sharedTable))
            return false;

        if (!_instances.SetMaterialTable(handle, sharedTable, _materialTables.GetOffset(sharedTable),
                                         _materialTables.GetCount(sharedTable), false))
        {
            _materialTables.Release(sharedTable);
            return false;
        }
        _materialTables.Release(oldTable);
        return true;
    }

    bool RenderScene::SetGeometryGroupEnabled(ModelInstanceHandle handle, u32 groupID, bool enabled)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        return resources && _geometryGroupMasks.SetEnabled(resources->geometryGroupMask, groupID, enabled);
    }

    bool RenderScene::SetAllGeometryGroups(ModelInstanceHandle handle, bool enabled)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        return resources && _geometryGroupMasks.SetAll(resources->geometryGroupMask, enabled);
    }

    SceneClearRequests RenderScene::GetPendingClearRequests() const
    {
        return { _instances.GetPendingSlotClears(), _meshletHistory.GetPendingClears() };
    }

    void RenderScene::AcknowledgeClearsAndPublish()
    {
        _instances.AcknowledgePendingSlotClears();
        _meshletHistory.AcknowledgePendingClears();
        _instances.PublishPending();
    }

    void RenderScene::ReleaseRetiredHistory(u64 completedValue)
    {
        _meshletHistory.ReleaseRetired(completedValue);
    }

    void RenderScene::AdvanceFrame()
    {
        _instances.AdvanceFrame();
    }

    void RenderScene::SyncToGPU(Renderer::Renderer* renderer)
    {
        _materialTables.SyncToGPU(renderer);
        _geometryGroupMasks.SyncToGPU(renderer);
        _instances.SyncToGPU(renderer);
    }

    RenderSceneStats RenderScene::GetStats() const
    {
        return { _instances.GetStats(), _materialTables.GetStats(), _geometryGroupMasks.GetStats(), _meshletHistory.GetStats() };
    }

    ModelMaterialTableHandle RenderScene::AcquireDefaultMaterialTable(RenderAssets::ModelHandle model) const
    {
        const ModelLoading::ModelGPURecord& record = _geometryStorage->GetRecord(model);
        std::vector<u32> materials(record.defaultMaterialTableCount);
        for (u32 index = 0; index < record.defaultMaterialTableCount; ++index)
            materials[index] = _materialStorage->GetMaterialTableEntry(record.defaultMaterialTableOffset + index);
        return _materialTables.AcquireShared(model, materials);
    }
} // namespace RenderScenes
