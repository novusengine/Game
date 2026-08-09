#include "RenderScene.h"

#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"

#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/Map/Map.h>

#include <vector>
#include <limits>

#include <tracy/Tracy.hpp>

namespace RenderScenes
{
    namespace
    {
        bool ToSceneCount(u64 value, const char* name, u32& out)
        {
            if (value > std::numeric_limits<u32>::max())
            {
                NC_LOG_WARNING("MODEL_ALLOCATION_HINT ignored resource={} count={} max={}", name, value,
                               std::numeric_limits<u32>::max());
                return false;
            }
            out = static_cast<u32>(value);
            return true;
        }
    }

    RenderScene::RenderScene(u64 sceneID, const ModelLoading::ModelGeometryStorage* geometryStorage,
                             const MaterialLoading::MaterialStorage* materialStorage, bool validateTransfers)
        : _sceneID(sceneID), _geometryStorage(geometryStorage), _materialStorage(materialStorage),
          _materialTables(validateTransfers), _geometryGroupMasks(validateTransfers), _instances(validateTransfers)
    {
    }

    void RenderScene::ReserveModelResources(const Map::ModelAllocationHints& hints)
    {
        u32 instanceCount = 0;
        if (ToSceneCount(hints.scene.totalModelInstances, "scene_instances", instanceCount))
        {
            _instances.Reserve(instanceCount);
            _meshletHistory.Reserve(instanceCount);
        }

        u32 groupWords = 0;
        if (ToSceneCount(hints.scene.geometryGroupMaskWords, "geometry_group_mask_words", groupWords))
            _geometryGroupMasks.Reserve(instanceCount, groupWords);

        u32 modelCount = 0;
        u32 materialSlots = 0;
        if (ToSceneCount(hints.resources.models, "scene_model_tables", modelCount) &&
            ToSceneCount(hints.resources.materialSlots, "scene_material_table_entries", materialSlots))
            _materialTables.Reserve(modelCount, materialSlots);
    }

    ModelInstanceHandle RenderScene::CreateModelInstance(const ModelInstanceDesc& desc)
    {
        ZoneScopedN("RenderScene::CreateModelInstance");

        if (!_geometryStorage || !_materialStorage || !_geometryStorage->HasModel(desc.model))
            return InvalidModelInstanceHandle();

        const ModelLoading::ModelGPURecord& model = _geometryStorage->GetRecord(desc.model);
        ModelMaterialTableHandle materialTable;
        {
            ZoneScopedN("Acquire Instance Material Table");
            materialTable = AcquireDefaultMaterialTable(desc.model);
        }
        if (!_materialTables.IsValid(materialTable))
            return InvalidModelInstanceHandle();

        GeometryGroupMaskHandle groupMask;
        {
            ZoneScopedN("Allocate Instance Geometry Groups");
            groupMask = _geometryGroupMasks.Create(model.geometryGroupCount);
        }
        ModelScene::MeshletHistoryRange history;
        {
            ZoneScopedN("Allocate Instance Meshlet History");
            history = _meshletHistory.Allocate((model.numMeshlets + 31u) / 32u);
        }

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
        {
            ZoneScopedN("Create Instance Record");
            return _instances.Create(createInfo);
        }
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

        if (!_materialTables.SetMaterial(table, slot, material))
            return false;

        RequestModelHistoryClear(handle);
        return true;
    }

    bool RenderScene::SetModelMaterials(ModelInstanceHandle handle,
                                        std::span<const RenderAssets::MaterialInstanceHandle> materials)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!resources || materials.size() != _materialTables.GetCount(resources->materialTable))
            return false;

        std::vector<u32> entries;
        entries.reserve(materials.size());
        for (const RenderAssets::MaterialInstanceHandle material : materials)
        {
            if (!_materialStorage->HasMaterialInstance(material))
                return false;
            entries.push_back(static_cast<RenderAssets::MaterialInstanceHandle::type>(material));
        }

        const ModelMaterialTableHandle oldTable = resources->materialTable;
        const ModelMaterialTableHandle sharedTable = _materialTables.AcquireShared(entries);
        if (!_materialTables.IsValid(sharedTable))
            return false;

        if (!_instances.SetMaterialTable(handle, sharedTable, _materialTables.GetOffset(sharedTable),
                                         _materialTables.GetCount(sharedTable), false))
        {
            _materialTables.Release(sharedTable);
            return false;
        }
        _materialTables.Release(oldTable);
        RequestModelHistoryClear(handle);
        return true;
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
        RequestModelHistoryClear(handle);
        return true;
    }

    bool RenderScene::SetGeometryGroupEnabled(ModelInstanceHandle handle, u32 groupID, bool enabled)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!resources || !_geometryGroupMasks.SetEnabled(resources->geometryGroupMask, groupID, enabled))
            return false;

        RequestModelHistoryClear(handle);
        return true;
    }

    bool RenderScene::SetAllGeometryGroups(ModelInstanceHandle handle, bool enabled)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!resources || !_geometryGroupMasks.SetAll(resources->geometryGroupMask, enabled))
            return false;

        RequestModelHistoryClear(handle);
        return true;
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
        ZoneScopedN("RenderScene::SyncToGPU");
        {
            ZoneScopedN("Sync Scene Material Tables");
            _materialTables.SyncToGPU(renderer);
        }
        {
            ZoneScopedN("Sync Scene Geometry Groups");
            _geometryGroupMasks.SyncToGPU(renderer);
        }
        {
            ZoneScopedN("Sync Scene Instances");
            _instances.SyncToGPU(renderer);
        }
    }

    RenderSceneStats RenderScene::GetStats() const
    {
        return { _instances.GetStats(), _materialTables.GetStats(), _geometryGroupMasks.GetStats(), _meshletHistory.GetStats() };
    }

    void RenderScene::RequestModelHistoryClear(ModelInstanceHandle handle)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (resources)
            _meshletHistory.RequestClear(resources->meshletHistory);
    }

    ModelMaterialTableHandle RenderScene::AcquireDefaultMaterialTable(RenderAssets::ModelHandle model) const
    {
        const ModelLoading::ModelGPURecord& record = _geometryStorage->GetRecord(model);
        std::vector<u32> materials(record.defaultMaterialTableCount);
        for (u32 index = 0; index < record.defaultMaterialTableCount; ++index)
            materials[index] = _materialStorage->GetMaterialTableEntry(record.defaultMaterialTableOffset + index);
        return _materialTables.AcquireShared(materials);
    }
} // namespace RenderScenes
