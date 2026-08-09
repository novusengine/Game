#include "UnitPreview.h"

#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Gameplay/Database/Unit.h"
#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/ModelParameterOverrides.h"
#include "Game-Lib/Rendering/Model/Scene/ModelSceneBridge.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/OffscreenRenderView.h"
#include "Game-Lib/Rendering/Scene/OrbitCamera.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Renderer/Renderer.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Unit/Unit.h>

#include <xxhash/xxhash64.h>

#include <algorithm>
#include <functional>
#include <utility>

namespace PreviewRendering
{
    namespace
    {
        constexpr u64 PREVIEW_SCENE_ID = 2;
        constexpr u32 DEVELOPMENT_GALLERY_APPEARANCE_COUNT = 3;
        constexpr u32 DEVELOPMENT_GALLERY_SETTLE_FRAMES = 90;

        constexpr std::array<u32, 4> DEVELOPMENT_GALLERY_EQUIPMENT_SLOTS = {
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Chest),
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Gloves),
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Pants),
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Boots)
        };

    }

    UnitPreview::UnitPreview(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                             RenderAssets::RenderAssetResources* assets,
                             ModelScene::ModelSceneBridge* worldBridge, RenderScenes::RenderScene* worldScene,
                             RenderResources& resources, bool validateTransfers)
        : _renderer(renderer), _assets(assets), _worldBridge(worldBridge), _worldScene(worldScene),
          _resources(&resources), _validateTransfers(validateTransfers)
    {
        _scene = std::make_unique<RenderScenes::RenderScene>(PREVIEW_SCENE_ID, &_assets->GetModelGeometryStorage(),
                                                             &_assets->GetMaterialStorage(), _validateTransfers);
        RenderScenes::RenderViewDesc viewDesc;
        viewDesc.debugName = "Unit Preview";
        viewDesc.scene = _scene.get();
        viewDesc.cameraIndex = RenderScenes::INVALID_RENDER_VIEW_CAMERA;
        viewDesc.passFamilies = RenderScenes::RenderViewPassFamily::Models;
        viewDesc.lifetime = RenderScenes::RenderViewLifetime::Persistent;
        viewDesc.refresh = RenderScenes::RenderViewRefresh::Retained;
        viewDesc.clearTargets = true;
        _renderView = std::make_unique<RenderScenes::OffscreenRenderView>(renderer, gameRenderer, std::move(viewDesc));
    }

    UnitPreview::~UnitPreview() = default;

    bool UnitPreview::SetTarget(Renderer::TextureID target)
    {
        if (!_renderView->SetTarget(target))
            return false;
        if (!_camera)
            _camera = std::make_unique<RenderScenes::OrbitCamera>(*_resources, *_renderView->GetView());
        return true;
    }

    void UnitPreview::SetUnit(entt::entity unit)
    {
        if (_unit == unit)
            return;
        _unit = unit;
        _developmentGallery.retained = false;
        _sourceInstances.clear();
        _reportedMissingSource = false;
        if (RenderScenes::RenderView* view = _renderView->GetView())
            view->MarkDirty();
    }

    bool UnitPreview::BeginDevelopmentGallery(entt::registry& registry, entt::entity unit)
    {
        RenderScenes::RenderView* view = _renderView->GetView();
        if (!view || unit == entt::null || !registry.valid(unit))
        {
            NC_LOG_WARNING("UNIT_PREVIEW development_gallery_invalid_source view={} unit={} valid={}", view != nullptr, entt::to_integral(unit), registry.valid(unit));
            return false;
        }
        auto* equipment = registry.try_get<ECS::Components::UnitEquipment>(unit);
        if (!equipment)
        {
            NC_LOG_WARNING("UNIT_PREVIEW development_gallery_missing_equipment unit={}", entt::to_integral(unit));
            return false;
        }

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDB = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
        auto* itemStorage = clientDB.Get(ClientDBHash::Item);
        auto* displayStorage = clientDB.Get(ClientDBHash::ItemDisplayInfo);
        if (!itemStorage || !displayStorage)
        {
            NC_LOG_WARNING("UNIT_PREVIEW development_gallery_missing_item_data items={} displays={}", itemStorage != nullptr, displayStorage != nullptr);
            return false;
        }

        const u32 equipmentCount = static_cast<u32>(equipment->equipmentSlotToVisualItemID.size());
        std::array<std::vector<u32>, static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::EquipmentEnd) + 1u> candidates;
        const auto& itemData = dbRegistry->ctx().get<ECS::Singletons::ItemSingleton>();
        displayStorage->Each([&](u32 displayID, const MetaGen::Shared::ClientDB::ItemDisplayInfoRecord&) {
            const auto sectionsIt = itemData.itemDisplayInfoToComponentSectionData.find(displayID);
            if (sectionsIt == itemData.itemDisplayInfoToComponentSectionData.end())
                return true;
            const auto& sections = sectionsIt->second.componentSectionToTextureHash;
            auto hasSection = [&](Database::Unit::TextureSectionType section) { return sections.contains(static_cast<u8>(section)); };
            if (hasSection(Database::Unit::TextureSectionType::Hand))
                candidates[static_cast<u32>(Database::Item::ItemEquipSlot::Gloves)].push_back(displayID);
            if (hasSection(Database::Unit::TextureSectionType::TorsoUpper))
                candidates[static_cast<u32>(Database::Item::ItemEquipSlot::Chest)].push_back(displayID);
            if (hasSection(Database::Unit::TextureSectionType::LegUpper))
                candidates[static_cast<u32>(Database::Item::ItemEquipSlot::Pants)].push_back(displayID);
            if (hasSection(Database::Unit::TextureSectionType::Foot))
                candidates[static_cast<u32>(Database::Item::ItemEquipSlot::Boots)].push_back(displayID);
            return true;
        });
        for (auto& slotCandidates : candidates)
            std::sort(slotCandidates.begin(), slotCandidates.end());

        _developmentGallery.originalEquipment.assign(equipment->equipmentSlotToVisualItemID.begin(),
                                                      equipment->equipmentSlotToVisualItemID.end());
        for (u32 appearance = 0; appearance < DEVELOPMENT_GALLERY_APPEARANCE_COUNT; ++appearance)
        {
            auto& selection = _developmentGallery.appearances[appearance];
            selection.assign(equipmentCount, 0u);
            for (u32 slot : DEVELOPMENT_GALLERY_EQUIPMENT_SLOTS)
            {
                if (slot >= candidates.size() || candidates[slot].empty())
                    continue;
                const auto& slotCandidates = candidates[slot];
                const u64 mixed = (static_cast<u64>(appearance) + 1u) * 0x9E3779B185EBCA87ull ^
                                  (static_cast<u64>(slot) + 1u) * 0xC2B2AE3D27D4EB4Full;
                MetaGen::Shared::ClientDB::ItemRecord item = {};
                item.displayID = slotCandidates[mixed % slotCandidates.size()];
                selection[slot] = itemStorage->Add(item);
            }
        }

        for (RenderScenes::ModelInstanceHandle handle : _previewInstances)
            _scene->DestroyModelInstance(handle, 0);
        _scene->ReleaseRetiredHistory(0);
        _previewInstances.clear();
        for (Renderer::TextureID texture : _developmentGalleryTextures)
            _renderer->UnloadTexture(texture);
        _developmentGalleryTextures.clear();

        _unit = unit;
        _sourceInstances.clear();
        _developmentGallery.appearanceIndex = 0;
        _developmentGallery.waitFrames = DEVELOPMENT_GALLERY_SETTLE_FRAMES;
        _developmentGallery.active = true;
        _developmentGallery.retained = true;
        ApplyDevelopmentGalleryAppearance(registry, 0);
        return true;
    }

    void UnitPreview::ApplyDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex)
    {
        auto* equipment = registry.try_get<ECS::Components::UnitEquipment>(_unit);
        if (!equipment || appearanceIndex >= _developmentGallery.appearances.size())
            return;
        const auto& selection = _developmentGallery.appearances[appearanceIndex];
        for (u32 slot = 0; slot < equipment->equipmentSlotToVisualItemID.size(); ++slot)
        {
            equipment->equipmentSlotToVisualItemID[slot] = selection[slot];
            equipment->dirtyVisualItemIDSlots.insert(static_cast<MetaGen::Shared::Unit::ItemEquipSlotEnum>(slot));
        }
        registry.emplace_or_replace<ECS::Components::UnitVisualEquipmentDirty>(_unit);
    }

    bool UnitPreview::CaptureDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex)
    {
        std::vector<SourceInstance> instances;
        CollectSourceInstances(registry, instances);
        if (instances.empty())
            return false;

        const ECS::Components::Transform* rootTransform = registry.try_get<ECS::Components::Transform>(_unit);
        const ModelScene::ModelInstanceGPURecord* rootRecord = _worldScene->GetModelInstance(instances.front().handle);
        if (!rootTransform || !rootRecord)
            return false;

        const ModelLoading::ModelGPURecord& rootModel =
            _assets->GetModelGeometryStorage().GetRecord(RenderAssets::ModelHandle(rootRecord->modelIndex));
        const f32 diameter = std::max(rootModel.bounds.sphereRadius * 2.0f, 0.001f);
        const f32 scale = 1.85f / diameter;
        const f32 horizontalOffset = (static_cast<f32>(appearanceIndex) - 1.0f) * 1.15f;
        mat4x4 previewRoot(scale);
        previewRoot[3] = vec4(-rootModel.bounds.center * scale + vec3(horizontalOffset, 0.0f, 0.0f), 1.0f);
        _camera->SetDistance(4.2f);

        Renderer::TextureID frozenSkin = Renderer::TextureID::Invalid();
        if (const auto* customization = registry.try_get<ECS::Components::UnitCustomization>(_unit);
            customization && customization->skinTextureID != Renderer::TextureID::Invalid())
        {
            frozenSkin = ServiceLocator::GetGameRenderer()->GetTextureRenderer()->MakeRenderableCopy(customization->skinTextureID, 512, 512);
            _developmentGalleryTextures.push_back(frozenSkin);
        }

        const mat4x4 inverseRoot = glm::inverse(rootTransform->GetMatrix());
        std::vector<RenderAssets::MaterialInstanceHandle> materials;
        for (const SourceInstance& source : instances)
        {
            const ModelScene::ModelInstanceResources* sourceResources = _worldScene->GetModelInstances().GetResources(source.handle);
            const ECS::Components::Transform* sourceTransform = registry.try_get<ECS::Components::Transform>(source.entity);
            if (!sourceResources || !sourceTransform)
                continue;

            RenderScenes::ModelInstanceDesc desc;
            desc.model = sourceResources->model;
            desc.worldTransform = previewRoot * inverseRoot * sourceTransform->GetMatrix();
            const RenderScenes::ModelInstanceHandle preview = _scene->CreateModelInstance(desc);
            if (!_scene->IsPending(preview))
                continue;

            const ModelScene::ModelMaterialTableStore& sourceTables = _worldScene->GetModelMaterialTables();
            const u32 materialCount = sourceTables.GetCount(sourceResources->materialTable);
            materials.clear();
            materials.reserve(materialCount);
            for (u32 slot = 0; slot < materialCount; ++slot)
                materials.emplace_back(sourceTables.GetMaterial(sourceResources->materialTable, slot));
            _scene->SetModelMaterials(preview, materials);

            const u32 groupCount = _assets->GetModelGeometryStorage().GetRecord(sourceResources->model).geometryGroupCount;
            for (u32 group = 0; group < groupCount; ++group)
            {
                const bool enabled = _worldScene->GetGeometryGroupMasks().IsEnabled(sourceResources->geometryGroupMask, group);
                _scene->SetGeometryGroupEnabled(preview, group, enabled);
            }
            if (source.entity == _unit && frozenSkin != Renderer::TextureID::Invalid())
            {
                ServiceLocator::GetGameRenderer()->GetModelParameterOverrides()->SetTexture(
                    *_scene, preview, sourceResources->model, XXHash64::hash("Skin", 4, 0), frozenSkin);
            }
            _previewInstances.push_back(preview);
        }

        _renderView->GetView()->RequestTemporalReset();
        return true;
    }

    void UnitPreview::RestoreDevelopmentGallerySource(entt::registry& registry)
    {
        auto* equipment = registry.try_get<ECS::Components::UnitEquipment>(_unit);
        if (!equipment || _developmentGallery.originalEquipment.size() != equipment->equipmentSlotToVisualItemID.size())
            return;
        for (u32 slot = 0; slot < equipment->equipmentSlotToVisualItemID.size(); ++slot)
        {
            equipment->equipmentSlotToVisualItemID[slot] = _developmentGallery.originalEquipment[slot];
            equipment->dirtyVisualItemIDSlots.insert(static_cast<MetaGen::Shared::Unit::ItemEquipSlotEnum>(slot));
        }
        registry.emplace_or_replace<ECS::Components::UnitVisualEquipmentDirty>(_unit);
    }

    void UnitPreview::Orbit(f32 deltaYaw, f32 deltaPitch)
    {
        if (_camera)
            _camera->Orbit(deltaYaw, deltaPitch);
    }

    void UnitPreview::CollectSourceInstances(entt::registry& registry,
                                             std::vector<SourceInstance>& instances) const
    {
        instances.clear();
        if (_unit == entt::null || !registry.valid(_unit))
            return;

        std::function<void(entt::entity)> collect = [&](entt::entity entity) {
            const RenderScenes::ModelInstanceHandle handle = _worldBridge->Get(entity);
            if (_worldScene->IsAlive(handle))
            {
                const ModelScene::ModelInstanceResources* resources = _worldScene->GetModelInstances().GetResources(handle);
                if (resources)
                {
                    u64 appearanceHash = 14695981039346656037ull;
                    auto hashValue = [&appearanceHash](u64 value) {
                        appearanceHash ^= value;
                        appearanceHash *= 1099511628211ull;
                    };
                    const ModelScene::ModelMaterialTableStore& tables = _worldScene->GetModelMaterialTables();
                    const u32 materialCount = tables.GetCount(resources->materialTable);
                    for (u32 slot = 0; slot < materialCount; ++slot)
                        hashValue(static_cast<RenderAssets::MaterialInstanceHandle::type>(tables.GetMaterial(resources->materialTable, slot)));

                    const u32 groupCount = _assets->GetModelGeometryStorage().GetRecord(resources->model).geometryGroupCount;
                    for (u32 group = 0; group < groupCount; ++group)
                        hashValue(_worldScene->GetGeometryGroupMasks().IsEnabled(resources->geometryGroupMask, group));
                    instances.push_back({.entity = entity, .handle = handle, .appearanceHash = appearanceHash});
                }
            }
            ECS::TransformSystem::Get(registry).IterateChildren(entity, [&](ECS::Components::SceneNode* child) {
                collect(child->GetOwnerEntity());
            });
        };
        collect(_unit);
    }

    void UnitPreview::RebuildScene(entt::registry& registry, const std::vector<SourceInstance>& instances)
    {
        for (RenderScenes::ModelInstanceHandle handle : _previewInstances)
            _scene->DestroyModelInstance(handle, 0);
        _scene->ReleaseRetiredHistory(0);
        _previewInstances.clear();

        if (instances.empty())
            return;

        const ECS::Components::Transform* rootTransform = registry.try_get<ECS::Components::Transform>(_unit);
        const ModelScene::ModelInstanceGPURecord* rootRecord = _worldScene->GetModelInstance(instances.front().handle);
        if (!rootTransform || !rootRecord)
            return;

        const ModelLoading::ModelGPURecord& rootModel =
            _assets->GetModelGeometryStorage().GetRecord(RenderAssets::ModelHandle(rootRecord->modelIndex));
        const f32 diameter = std::max(rootModel.bounds.sphereRadius * 2.0f, 0.001f);
        const f32 scale = 2.2f / diameter;
        mat4x4 previewRoot(scale);
        previewRoot[3] = vec4(-rootModel.bounds.center * scale, 1.0f);
        _camera->SetDistance(std::max(rootModel.bounds.sphereRadius * scale * 2.8f, 2.2f));

        const mat4x4 inverseRoot = glm::inverse(rootTransform->GetMatrix());
        std::vector<RenderAssets::MaterialInstanceHandle> materials;
        for (const SourceInstance& source : instances)
        {
            const ModelScene::ModelInstanceResources* sourceResources =
                _worldScene->GetModelInstances().GetResources(source.handle);
            const ECS::Components::Transform* sourceTransform =
                registry.try_get<ECS::Components::Transform>(source.entity);
            if (!sourceResources || !sourceTransform)
                continue;

            RenderScenes::ModelInstanceDesc desc;
            desc.model = sourceResources->model;
            desc.worldTransform = previewRoot * inverseRoot * sourceTransform->GetMatrix();
            const RenderScenes::ModelInstanceHandle preview = _scene->CreateModelInstance(desc);
            if (!_scene->IsPending(preview))
                continue;

            const ModelScene::ModelMaterialTableStore& sourceTables = _worldScene->GetModelMaterialTables();
            const u32 materialCount = sourceTables.GetCount(sourceResources->materialTable);
            materials.clear();
            materials.reserve(materialCount);
            for (u32 slot = 0; slot < materialCount; ++slot)
                materials.emplace_back(sourceTables.GetMaterial(sourceResources->materialTable, slot));
            _scene->SetModelMaterials(preview, materials);

            const u32 groupCount = _assets->GetModelGeometryStorage().GetRecord(sourceResources->model).geometryGroupCount;
            for (u32 group = 0; group < groupCount; ++group)
            {
                const bool enabled = _worldScene->GetGeometryGroupMasks().IsEnabled(
                    sourceResources->geometryGroupMask, group);
                _scene->SetGeometryGroupEnabled(preview, group, enabled);
            }
            _previewInstances.push_back(preview);
        }

        _sourceInstances = instances;
        _renderView->GetView()->RequestTemporalReset();
    }

    void UnitPreview::Update(entt::registry& registry)
    {
        if (!_renderView->GetView())
            return;
        if (_developmentGallery.active)
        {
            if (_developmentGallery.waitFrames > 0)
            {
                --_developmentGallery.waitFrames;
                return;
            }
            if (!CaptureDevelopmentGalleryAppearance(registry, _developmentGallery.appearanceIndex))
            {
                RestoreDevelopmentGallerySource(registry);
                _developmentGallery.active = false;
                NC_LOG_ERROR("UNIT_PREVIEW development_gallery_failed appearance={}", _developmentGallery.appearanceIndex);
                return;
            }
            ++_developmentGallery.appearanceIndex;
            if (_developmentGallery.appearanceIndex < DEVELOPMENT_GALLERY_APPEARANCE_COUNT)
            {
                ApplyDevelopmentGalleryAppearance(registry, _developmentGallery.appearanceIndex);
                _developmentGallery.waitFrames = DEVELOPMENT_GALLERY_SETTLE_FRAMES;
            }
            else
            {
                RestoreDevelopmentGallerySource(registry);
                _developmentGallery.active = false;
                NC_LOG_INFO("UNIT_PREVIEW development_gallery_ready appearances={} view={}",
                            DEVELOPMENT_GALLERY_APPEARANCE_COUNT, _renderView->GetView()->GetID());
            }
            return;
        }
        if (_developmentGallery.retained)
            return;
        std::vector<SourceInstance> instances;
        CollectSourceInstances(registry, instances);
        if (instances.empty())
        {
            if (!_reportedMissingSource)
            {
                NC_LOG_WARNING("UNIT_PREVIEW no_renderable_source unit={}", entt::to_integral(_unit));
                _reportedMissingSource = true;
            }
            return;
        }
        _reportedMissingSource = false;
        if (instances != _sourceInstances)
            RebuildScene(registry, instances);
    }
} // namespace PreviewRendering
