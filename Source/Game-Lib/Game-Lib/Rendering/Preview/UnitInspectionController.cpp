#include "UnitInspectionController.h"

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
#include "Game-Lib/Rendering/Scene/ScenePreview.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Unit/Unit.h>

#include <xxhash/xxhash64.h>

#include <algorithm>
#include <functional>

namespace PreviewRendering
{
    namespace
    {
        constexpr u64 INSPECTION_SCENE_ID = 3;
        constexpr u32 DEVELOPMENT_GALLERY_APPEARANCE_COUNT = 3;
        constexpr u32 DEVELOPMENT_GALLERY_SETTLE_FRAMES = 90;

        constexpr std::array<u32, 4> DEVELOPMENT_GALLERY_EQUIPMENT_SLOTS = {
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Chest),
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Gloves),
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Pants),
            static_cast<u32>(MetaGen::Shared::Unit::ItemEquipSlotEnum::Boots)
        };

        void HashBytes(u64& hash, const void* data, size_t size)
        {
            hash = XXHash64::hash(data, size, hash);
        }

        void HashModelDescription(u64& hash, const RenderScenes::ModelRenderDescription& description)
        {
            const auto model = static_cast<RenderAssets::ModelHandle::type>(description.model);
            HashBytes(hash, &model, sizeof(model));
            HashBytes(hash, &description.transform, sizeof(description.transform));
            HashBytes(hash, description.materials.data(), description.materials.size() * sizeof(description.materials[0]));
            HashBytes(hash, description.enabledGeometryGroups.data(), description.enabledGeometryGroups.size() * sizeof(u32));
            HashBytes(hash, &description.opacity, sizeof(description.opacity));
            HashBytes(hash, &description.visible, sizeof(description.visible));
        }
    }

    UnitInspectionController::UnitInspectionController(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                                       RenderAssets::RenderAssetResources* assets,
                                                       ModelScene::ModelSceneBridge* worldBridge,
                                                       RenderResources& resources, bool validateTransfers)
        : _renderer(renderer), _assets(assets), _worldBridge(worldBridge)
    {
        _preview = std::make_unique<RenderScenes::ScenePreview>(renderer, gameRenderer, assets, resources,
                                                               "Unit", INSPECTION_SCENE_ID,
                                                               validateTransfers);
    }

    UnitInspectionController::~UnitInspectionController() = default;

    bool UnitInspectionController::SetTarget(Renderer::TextureID target)
    {
        return _preview->SetTarget(target);
    }

    void UnitInspectionController::SetUnit(entt::entity unit)
    {
        if (_unit == unit)
            return;
        _unit = unit;
        _developmentGallery.retained = false;
        _reportedMissingSource = false;
        _preview->Clear();
    }

    void UnitInspectionController::Orbit(f32 deltaYaw, f32 deltaPitch)
    {
        _preview->Orbit(deltaYaw, deltaPitch);
    }

    bool UnitInspectionController::BuildDescription(entt::registry& registry, entt::entity unit,
                                                     UnitRenderDescription& description, f32 horizontalOffset,
                                                     Renderer::TextureID frozenSkin) const
    {
        if (unit == entt::null || !registry.valid(unit))
            return false;
        const auto* rootTransform = registry.try_get<ECS::Components::Transform>(unit);
        if (!rootTransform)
            return false;

        RenderScenes::ModelRenderDescription root;
        if (!_worldBridge->Describe(unit, root))
            return false;
        const ModelLoading::ModelGPURecord& rootModel = _assets->GetModelGeometryStorage().GetRecord(root.model);
        const f32 diameter = std::max(rootModel.bounds.sphereRadius * 2.0f, 0.001f);
        const f32 scale = 2.2f / diameter;
        mat4x4 previewRoot(scale);
        previewRoot[3] = vec4(-rootModel.bounds.center * scale + vec3(horizontalOffset, 0.0f, 0.0f), 1.0f);
        const mat4x4 inverseRoot = glm::inverse(rootTransform->GetMatrix());

        description = {};
        description.scene.boundsCenter = vec3(horizontalOffset, 0.0f, 0.0f);
        description.scene.boundsRadius = std::max(rootModel.bounds.sphereRadius * scale, 0.1f);
        u64 revision = 14695981039346656037ull;

        std::function<void(entt::entity)> collect = [&](entt::entity entity) {
            RenderScenes::ModelRenderDescription part;
            const auto* transform = registry.try_get<ECS::Components::Transform>(entity);
            if (_worldBridge->Describe(entity, part) && transform)
            {
                part.transform = previewRoot * inverseRoot * transform->GetMatrix();
                if (entity == unit && frozenSkin != Renderer::TextureID::Invalid())
                {
                    ServiceLocator::GetGameRenderer()->GetModelParameterOverrides()->SetTexture(
                        part, XXHash64::hash("Skin", 4, 0), frozenSkin);
                }
                HashModelDescription(revision, part);
                description.scene.models.push_back(std::move(part));
            }
            ECS::TransformSystem::Get(registry).IterateChildren(entity, [&](ECS::Components::SceneNode* child) {
                collect(child->GetOwnerEntity());
            });
        };
        collect(unit);

        if (const auto* customization = registry.try_get<ECS::Components::UnitCustomization>(unit))
        {
            const std::array values = {customization->skinID, customization->faceID, customization->facialHairID,
                                       customization->hairStyleID, customization->hairColorID, customization->earringsID,
                                       customization->piercingsID, customization->tattoosID, customization->featuresID,
                                       customization->tusksID, customization->hornStyleID, customization->hornColorID};
            HashBytes(revision, values.data(), values.size());
        }
        if (const auto* equipment = registry.try_get<ECS::Components::UnitEquipment>(unit))
            HashBytes(revision, equipment->equipmentSlotToVisualItemID.data(), sizeof(equipment->equipmentSlotToVisualItemID));

        description.scene.revision = revision == 0 ? 1 : revision;
        return !description.scene.models.empty();
    }

    void UnitInspectionController::Update(entt::registry& registry)
    {
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
                NC_LOG_ERROR("UNIT_INSPECTION development_gallery_failed appearance={}", _developmentGallery.appearanceIndex);
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
                NC_LOG_INFO("UNIT_INSPECTION development_gallery_ready appearances={}", DEVELOPMENT_GALLERY_APPEARANCE_COUNT);
            }
            return;
        }
        if (_developmentGallery.retained)
            return;
        if (_unit == entt::null)
            return;

        UnitRenderDescription description;
        if (!BuildDescription(registry, _unit, description))
        {
            if (!_reportedMissingSource)
            {
                NC_LOG_WARNING("UNIT_INSPECTION no_renderable_source unit={}", entt::to_integral(_unit));
                _reportedMissingSource = true;
            }
            return;
        }
        _reportedMissingSource = false;
        _preview->SetContent(description.scene);
    }

    bool UnitInspectionController::BeginDevelopmentGallery(entt::registry& registry, entt::entity unit)
    {
        if (unit == entt::null || !registry.valid(unit))
            return false;
        auto* equipment = registry.try_get<ECS::Components::UnitEquipment>(unit);
        if (!equipment)
            return false;

        entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& clientDB = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
        auto* itemStorage = clientDB.Get(ClientDBHash::Item);
        auto* displayStorage = clientDB.Get(ClientDBHash::ItemDisplayInfo);
        if (!itemStorage || !displayStorage)
            return false;

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

        _developmentGallery.originalEquipment.assign(equipment->equipmentSlotToVisualItemID.begin(), equipment->equipmentSlotToVisualItemID.end());
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

        for (Renderer::TextureID texture : _developmentGalleryTextures)
            _renderer->UnloadTexture(texture);
        _developmentGalleryTextures.clear();
        _developmentGallery.description = {};
        _preview->Clear();
        _unit = unit;
        _developmentGallery.appearanceIndex = 0;
        _developmentGallery.waitFrames = DEVELOPMENT_GALLERY_SETTLE_FRAMES;
        _developmentGallery.active = true;
        _developmentGallery.retained = true;
        ApplyDevelopmentGalleryAppearance(registry, 0);
        return true;
    }

    void UnitInspectionController::ApplyDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex)
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

    bool UnitInspectionController::CaptureDevelopmentGalleryAppearance(entt::registry& registry, u32 appearanceIndex)
    {
        Renderer::TextureID frozenSkin = Renderer::TextureID::Invalid();
        if (const auto* customization = registry.try_get<ECS::Components::UnitCustomization>(_unit);
            customization && customization->skinTextureID != Renderer::TextureID::Invalid())
        {
            frozenSkin = ServiceLocator::GetGameRenderer()->GetTextureRenderer()->MakeRenderableCopy(customization->skinTextureID, 512, 512);
            _developmentGalleryTextures.push_back(frozenSkin);
        }

        UnitRenderDescription appearance;
        const f32 offset = (static_cast<f32>(appearanceIndex) - 1.0f) * 1.15f;
        if (!BuildDescription(registry, _unit, appearance, offset, frozenSkin))
            return false;
        auto& gallery = _developmentGallery.description.scene;
        gallery.models.insert(gallery.models.end(), std::make_move_iterator(appearance.scene.models.begin()),
                              std::make_move_iterator(appearance.scene.models.end()));
        gallery.boundsCenter = vec3(0.0f);
        gallery.boundsRadius = 2.2f;
        gallery.revision = appearance.scene.revision ^ (static_cast<u64>(appearanceIndex + 1u) << 56u);
        return _preview->SetContent(gallery);
    }

    void UnitInspectionController::RestoreDevelopmentGallerySource(entt::registry& registry)
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
} // namespace PreviewRendering
