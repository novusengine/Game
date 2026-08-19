#include "RenderScene.h"
#include "SceneRenderDescription.h"

#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"

#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/Map/Map.h>

#include <vector>
#include <limits>

#include <tracy/Tracy.hpp>

namespace RenderScenes
{
    bool RenderScene::DescribeModelInstance(ModelInstanceHandle handle, ModelRenderDescription& description) const
    {
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const ModelScene::ModelInstanceResources* instanceResources = _instances.GetResources(handle);
        if (!record || !instanceResources)
            return false;

        description = {};
        description.model = instanceResources->model;
        description.transform = record->currentWorld;
        description.visible = (record->flags & ModelScene::ModelInstanceFlagVisible) != 0;
        description.opacity = record->opacity;
        description.highlightIntensity = record->highlightIntensity;
        description.packedHighlightColor = record->packedHighlightColor;
        description.castsShadows = (record->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0;

        const u32 materialCount = _materialTables.GetCount(instanceResources->materialTable);
        description.materials.reserve(materialCount);
        for (u32 slot = 0; slot < materialCount; ++slot)
            description.materials.emplace_back(_materialTables.GetMaterial(instanceResources->materialTable, slot));

        const ModelLoading::ModelGPURecord& model = _geometryStorage->GetRecord(instanceResources->model);
        description.enabledGeometryGroups.reserve(model.geometryGroupCount);
        for (u32 group = 0; group < model.geometryGroupCount; ++group)
        {
            if (_geometryGroupMasks.IsEnabled(instanceResources->geometryGroupMask, group))
                description.enabledGeometryGroups.push_back(group);
        }
        return true;
    }

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
        createInfo.meshletCount = model.numMeshlets;
        createInfo.visible = desc.visible;
        {
            ZoneScopedN("Create Instance Record");
            const ModelInstanceHandle handle = _instances.Create(createInfo);
            if (IsPending(handle))
                _shadowState.ModelCreated(handle, model.bounds, desc.worldTransform, desc.visible);
            return handle;
        }
    }

    bool RenderScene::DestroyModelInstance(ModelInstanceHandle handle)
    {
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const ModelScene::ModelInstanceResources* instanceResources = _instances.GetResources(handle);
        const bool hasShadowState = record && instanceResources && _geometryStorage->HasModel(instanceResources->model);
        const FileFormat::Model::Bounds bounds = hasShadowState ? _geometryStorage->GetRecord(instanceResources->model).bounds : FileFormat::Model::Bounds{};
        const mat4x4 transform = hasShadowState ? record->currentWorld : mat4x4(1.0f);
        const bool visible = hasShadowState && _instances.IsVisible(handle) && (record->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0;
        const bool wasHighlighted = record && record->highlightIntensity != 1.0f;

        ModelScene::ModelInstanceResources resources;
        if (!_instances.Destroy(handle, resources))
            return false;

        if (hasShadowState)
            _shadowState.ModelDestroyed(handle, bounds, transform, visible);

        _materialTables.Release(resources.materialTable);
        _geometryGroupMasks.Release(resources.geometryGroupMask);
        _meshletHistory.Retire(resources.meshletHistory, _historyRetireValue);
        _transparentHighlightsDirty |= wasHighlighted;
        return true;
    }

    bool RenderScene::SetModelHighlight(ModelInstanceHandle handle, f32 intensity, u32 packedColor)
    {
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        if (!record)
            return false;
        const bool changed = record->highlightIntensity != intensity;
        if (!_instances.SetHighlight(handle, intensity, packedColor))
            return false;
        _transparentHighlightsDirty |= changed;
        return true;
    }

    bool RenderScene::SetModelTransform(ModelInstanceHandle handle, const mat4x4& transform, bool teleported)
    {
        const ModelScene::ModelInstanceGPURecord* oldRecord = _instances.GetRecord(handle);
        const ModelScene::ModelInstanceResources* instanceResources = _instances.GetResources(handle);
        const bool hasShadowState = oldRecord && instanceResources && _geometryStorage->HasModel(instanceResources->model);
        const mat4x4 oldTransform = hasShadowState ? oldRecord->currentWorld : transform;
        const bool visible = hasShadowState && _instances.IsVisible(handle) && (oldRecord->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0;

        bool needsHistoryClear = false;
        if (!_instances.SetTransform(handle, transform, teleported, needsHistoryClear))
            return false;

        if (hasShadowState)
        {
            const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(instanceResources->model).bounds;
            if (_shadowState.ModelTransformChanged(handle, bounds, oldTransform, transform, visible))
                _instances.SetShadowDynamic(handle, true);
        }

        if (needsHistoryClear)
        {
            const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
            _meshletHistory.RequestClear(resources->meshletHistory);
        }
        return true;
    }

    bool RenderScene::SetModelVisible(ModelInstanceHandle handle, bool visible)
    {
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const ModelScene::ModelInstanceResources* instanceResources = _instances.GetResources(handle);
        const bool hasShadowState = record && instanceResources && _geometryStorage->HasModel(instanceResources->model);
        const bool castsShadows = hasShadowState && (record->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0;
        const bool oldVisible = hasShadowState && _instances.IsVisible(handle) && castsShadows;
        const mat4x4 transform = hasShadowState ? record->currentWorld : mat4x4(1.0f);

        bool needsHistoryClear = false;
        if (!_instances.SetVisible(handle, visible, needsHistoryClear))
            return false;

        if (hasShadowState)
        {
            const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(instanceResources->model).bounds;
            if (_shadowState.ModelVisibilityChanged(handle, bounds, transform, oldVisible, visible && castsShadows))
                _instances.SetShadowDynamic(handle, false);
        }

        if (needsHistoryClear)
        {
            const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
            _meshletHistory.RequestClear(resources->meshletHistory);
        }
        return true;
    }

    bool RenderScene::SetModelOpacity(ModelInstanceHandle handle, f32 opacity, bool forceTransparent)
    {
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!record || !resources)
            return false;

        const bool visible = (record->flags & ModelScene::ModelInstanceFlagVisible) != 0;
        const bool castsShadows = (record->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0;
        const f32 oldOpacity = record->opacity;
        const bool oldForceTransparent = (record->flags & ModelScene::ModelInstanceFlagForceTransparent) != 0;
        const bool oldShadowVisible = visible && castsShadows && record->opacity > 0.0f;
        const mat4x4 transform = record->currentWorld;
        if (!_instances.SetOpacity(handle, opacity, forceTransparent))
            return false;
        _transparentHighlightsDirty |= record->highlightIntensity != 1.0f && oldOpacity != opacity;

        record = _instances.GetRecord(handle);
        if (oldForceTransparent != ((record->flags & ModelScene::ModelInstanceFlagForceTransparent) != 0))
        {
            ++_transparentRoutingRevision;
            _instances.QueueRoutingChange(handle);
        }
        const bool newShadowVisible = visible && castsShadows && record->opacity > 0.0f;
        if (oldOpacity != record->opacity && _geometryStorage->HasModel(resources->model))
        {
            const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(resources->model).bounds;
            if (oldShadowVisible != newShadowVisible)
            {
                if (_shadowState.ModelVisibilityChanged(handle, bounds, transform, oldShadowVisible, newShadowVisible))
                    _instances.SetShadowDynamic(handle, false);
            }
            else
            {
                _shadowState.ModelAppearanceChanged(bounds, transform, newShadowVisible);
            }
        }
        return true;
    }

    bool RenderScene::SetModelMaterial(ModelInstanceHandle handle, u32 slot, RenderAssets::MaterialInstanceHandle material)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!resources || !_materialStorage->HasMaterialInstance(material))
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

        ++_transparentRoutingRevision;
        _instances.QueueRoutingChange(handle);
        _transparentHighlightsDirty = true;

        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(resources->model).bounds;
        _shadowState.ModelAppearanceChanged(bounds, record->currentWorld,
                                            (record->flags & ModelScene::ModelInstanceFlagVisible) != 0 &&
                                            (record->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0);
        RequestModelHistoryClear(handle);
        return true;
    }

    bool RenderScene::SetModelCastsShadows(ModelInstanceHandle handle, bool castsShadows)
    {
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!record || !resources)
            return false;

        const bool oldCastsShadows = (record->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0;
        if (oldCastsShadows == castsShadows)
            return true;
        const bool visible = (record->flags & ModelScene::ModelInstanceFlagVisible) != 0 && record->opacity > 0.0f;
        const mat4x4 transform = record->currentWorld;
        if (!_instances.SetCastsShadows(handle, castsShadows))
            return false;

        if (_geometryStorage->HasModel(resources->model))
        {
            const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(resources->model).bounds;
            if (_shadowState.ModelVisibilityChanged(handle, bounds, transform, visible && oldCastsShadows, visible && castsShadows))
                _instances.SetShadowDynamic(handle, false);
        }
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
        ++_transparentRoutingRevision;
        _instances.QueueRoutingChange(handle);
        _transparentHighlightsDirty = true;
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(resources->model).bounds;
        _shadowState.ModelAppearanceChanged(bounds, record->currentWorld,
                                            (record->flags & ModelScene::ModelInstanceFlagVisible) != 0);
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
        ++_transparentRoutingRevision;
        _instances.QueueRoutingChange(handle);
        _transparentHighlightsDirty = true;
        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(resources->model).bounds;
        _shadowState.ModelAppearanceChanged(bounds, record->currentWorld,
                                            (record->flags & ModelScene::ModelInstanceFlagVisible) != 0);
        RequestModelHistoryClear(handle);
        return true;
    }

    bool RenderScene::HasTransparentModelHighlights() const
    {
        if (!_transparentHighlightsDirty)
            return _hasTransparentHighlights;
        _transparentHighlightsDirty = false;
        _hasTransparentHighlights = false;
        if (!HasModelHighlights())
            return false;

        _instances.CollectHighlightedHandles(_highlightedModelScratch);
        for (const ModelInstanceHandle handle : _highlightedModelScratch)
        {
            const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
            const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
            if (!record || !resources || record->highlightIntensity == 1.0f)
                continue;
            if (record->opacity > 0.0f && record->opacity < 1.0f)
            {
                _hasTransparentHighlights = true;
                return true;
            }

            const u32 materialCount = _materialTables.GetCount(resources->materialTable);
            for (u32 slot = 0; slot < materialCount; ++slot)
            {
                const RenderAssets::MaterialInstanceHandle material(
                    static_cast<RenderAssets::MaterialInstanceHandle::type>(
                        _materialTables.GetMaterial(resources->materialTable, slot)));
                if (!_materialStorage->HasMaterialInstance(material))
                    continue;
                const MaterialLoading::MaterialInstanceGPURecord& instance =
                    _materialStorage->GetMaterialInstance(material);
                const u32 executionGroupClass = (instance.packedClassification >> 16u) %
                    FileFormat::Material::ABI::EXECUTION_GROUP_CLASS_COUNT;
                if (executionGroupClass / 2u == static_cast<u32>(FileFormat::Material::RasterClass::Transparent))
                {
                    _hasTransparentHighlights = true;
                    return true;
                }
            }
        }
        return false;
    }

    bool RenderScene::SetGeometryGroupEnabled(ModelInstanceHandle handle, u32 groupID, bool enabled)
    {
        return SetGeometryGroupRangeEnabled(handle, groupID, groupID, enabled);
    }

    bool RenderScene::SetGeometryGroupRangeEnabled(ModelInstanceHandle handle, u32 firstGroupID, u32 lastGroupID, bool enabled)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        if (!resources || firstGroupID > lastGroupID || lastGroupID >= _geometryGroupMasks.GetGroupCount(resources->geometryGroupMask))
            return false;
        bool changed = false;
        if (!_geometryGroupMasks.SetRangeEnabled(resources->geometryGroupMask, firstGroupID, lastGroupID, enabled, changed))
            return false;
        if (!changed)
            return true;

        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(resources->model).bounds;
        _shadowState.ModelAppearanceChanged(bounds, record->currentWorld,
                                            (record->flags & ModelScene::ModelInstanceFlagVisible) != 0);
        RequestModelHistoryClear(handle);
        return true;
    }

    bool RenderScene::SetAllGeometryGroups(ModelInstanceHandle handle, bool enabled)
    {
        const ModelScene::ModelInstanceResources* resources = _instances.GetResources(handle);
        bool changed = false;
        if (!resources || !_geometryGroupMasks.SetAll(resources->geometryGroupMask, enabled, changed))
            return false;
        if (!changed)
            return true;

        const ModelScene::ModelInstanceGPURecord* record = _instances.GetRecord(handle);
        const FileFormat::Model::Bounds& bounds = _geometryStorage->GetRecord(resources->model).bounds;
        _shadowState.ModelAppearanceChanged(bounds, record->currentWorld,
                                            (record->flags & ModelScene::ModelInstanceFlagVisible) != 0);
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
        _shadowState.AdvanceFrame(*this);
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

    void RenderScene::FlushModelResourceFrees()
    {
        _materialTables.FlushFrees();
        _geometryGroupMasks.FlushFrees();
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
        const u32 modelIndex = static_cast<RenderAssets::ModelHandle::type>(model);
        if (modelIndex >= _defaultMaterialTables.size())
            _defaultMaterialTables.resize(static_cast<size_t>(modelIndex) + 1u, InvalidModelMaterialTableHandle());
        ModelMaterialTableHandle& cached = _defaultMaterialTables[modelIndex];
        if (_materialTables.IsValid(cached))
        {
            _materialTables.AddReference(cached);
            return cached;
        }

        const ModelLoading::ModelGPURecord& record = _geometryStorage->GetRecord(model);
        std::vector<u32> materials(record.defaultMaterialTableCount);
        for (u32 index = 0; index < record.defaultMaterialTableCount; ++index)
            materials[index] = _materialStorage->GetMaterialTableEntry(record.defaultMaterialTableOffset + index);
        cached = _materialTables.AcquireShared(materials);
        if (_materialTables.IsValid(cached))
            _materialTables.AddReference(cached);
        return cached;
    }
} // namespace RenderScenes
