#include "DisplayResolver.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Game/ClientDB/ClientDB.h>

#include <xxhash/xxhash64.h>

#include <algorithm>
#include <array>

namespace ModelLoading
{
    size_t DisplayResolver::KeyHash::operator()(const Key& key) const
    {
        const std::array values = {static_cast<u64>(key.displayID), static_cast<u64>(key.source),
                                   static_cast<u64>(key.modelVariant)};
        return static_cast<size_t>(XXHash64::hash(values.data(), sizeof(values), 0));
    }

    bool DisplayResolver::Initialize(ClientDB::Data& registrations,
                                                ClientDB::Data& parameters)
    {
        ZoneScopedN("DisplayResolver::Initialize");

        _ranges.clear();
        _registrations.clear();
        _overrides.clear();
        robin_hood::unordered_map<u32, u32> registrationIndices;
        bool valid = true;
        registrations.Each([this, &registrationIndices, &valid](
            u32 id, const MetaGen::Game::ClientDB::DisplayRegistrationRecord& row) {
            if (id == 0)
                return true;
            if (row.source > static_cast<u8>(DisplaySource::ItemDisplayInfo) ||
                row.modelAssetID == FileFormat::INVALID_ASSET_ID)
            {
                valid = false;
                return false;
            }
            const Key key{.displayID = row.displayID,
                          .source = static_cast<DisplaySource>(row.source),
                          .modelVariant = row.modelVariant};
            auto [rangeIt, inserted] = _ranges.try_emplace(
                key, Range{.offset = static_cast<u32>(_registrations.size())});
            if (!inserted && rangeIt->second.offset + rangeIt->second.count != _registrations.size())
            {
                valid = false;
                return false;
            }
            registrationIndices.emplace(id, static_cast<u32>(_registrations.size()));
            _registrations.push_back({.modelAssetID = row.modelAssetID});
            ++rangeIt->second.count;
            return true;
        });

        parameters.Each([this, &registrationIndices, &valid](
            u32 id, const MetaGen::Game::ClientDB::DisplayParameterRecord& row) {
            if (id == 0)
                return true;
            const auto registration = registrationIndices.find(row.displayRegistrationID);
            if (registration == registrationIndices.end() ||
                row.type > static_cast<u8>(FileFormat::Model::ParameterType::Texture2D))
            {
                valid = false;
                return false;
            }
            Registration& selected = _registrations[registration->second];
            if (selected.overrideCount == 0)
                selected.overrideOffset = static_cast<u32>(_overrides.size());
            else if (selected.overrideOffset + selected.overrideCount != _overrides.size())
            {
                valid = false;
                return false;
            }
            _overrides.push_back({.value = {row.value0, row.value1},
                                  .stableID = row.modelParameterStableID,
                                  .type = static_cast<FileFormat::Model::ParameterType>(row.type)});
            ++selected.overrideCount;
            return true;
        });

        if (!valid)
        {
            _ranges.clear();
            _registrations.clear();
            _overrides.clear();
            NC_LOG_ERROR("MODEL_DISPLAY invalid_tables");
            return false;
        }

        NC_LOG_INFO("MODEL_DISPLAY indexed registrations={} parameters={}", _ranges.size(),
                    _overrides.size());
        return true;
    }

    DisplayApplyResult DisplayResolver::Apply(
        RenderScenes::RenderScene& scene, RenderScenes::ModelInstanceHandle instance,
        RenderAssets::ModelHandle model, FileFormat::AssetID modelAssetID,
        DisplaySource source, u32 displayID, u8 modelVariant)
    {
        ZoneScopedN("DisplayResolver::Apply");
        ++_applyRequests;

        if (!_assets || !_assets->GetModelGeometryStorage().HasModel(model))
        {
            ++_failures;
            return DisplayApplyResult::SceneUpdateFailed;
        }
        const auto rangeIt = _ranges.find({.displayID = displayID, .source = source,
                                           .modelVariant = modelVariant});
        if (rangeIt == _ranges.end())
        {
            ++_missingSelections;
            return DisplayApplyResult::SelectionNotFound;
        }
        const Range candidates = rangeIt->second;
        const Registration* selected = nullptr;
        for (u32 index = 0; index < candidates.count; ++index)
        {
            const Registration& candidate = _registrations[candidates.offset + index];
            if (candidate.modelAssetID == modelAssetID)
            {
                selected = &candidate;
                break;
            }
        }
        if (!selected)
        {
            ++_failures;
            return DisplayApplyResult::InvalidParameter;
        }

        const ModelGeometryStorage& geometry = _assets->GetModelGeometryStorage();
        const MaterialLoading::MaterialStorage& materialStorage = _assets->GetMaterialStorage();
        const ModelGPURecord& modelRecord = geometry.GetRecord(model);
        std::vector<RenderAssets::MaterialInstanceHandle> materials;
        materials.reserve(modelRecord.defaultMaterialTableCount);
        for (u32 slot = 0; slot < modelRecord.defaultMaterialTableCount; ++slot)
            materials.emplace_back(materialStorage.GetMaterialTableEntry(
                modelRecord.defaultMaterialTableOffset + slot));

        const std::span modelParameters = geometry.GetParameters(model);
        const std::span bindings = geometry.GetParameterBindings(model);
        std::vector<std::vector<MaterialLoading::MaterialTextureAssetOverride>> slotOverrides(
            modelRecord.numMaterialSlots);
        for (u32 index = 0; index < selected->overrideCount; ++index)
        {
            const ParameterOverride& overrideValue = _overrides[selected->overrideOffset + index];
            const auto parameter = std::find_if(modelParameters.begin(), modelParameters.end(),
                [&overrideValue](const FileFormat::Model::Parameter& candidate) {
                    return candidate.stableID == overrideValue.stableID;
                });
            if (parameter == modelParameters.end() || parameter->type != overrideValue.type ||
                overrideValue.type != FileFormat::Model::ParameterType::Texture2D)
            {
                ++_failures;
                return DisplayApplyResult::InvalidParameter;
            }
            for (const FileFormat::Model::ParameterBinding& binding : bindings)
            {
                if (binding.parameterStableID != overrideValue.stableID)
                    continue;
                if (binding.target != FileFormat::Model::ParameterBindingTarget::TextureSlot)
                {
                    ++_failures;
                    return DisplayApplyResult::InvalidParameter;
                }
                u32 materialSlot = 0;
                if (!geometry.FindMaterialSlot(model, binding.materialSlotStableID, materialSlot))
                {
                    ++_failures;
                    return DisplayApplyResult::InvalidMaterialSlot;
                }
                slotOverrides[materialSlot].push_back(
                    {.textureSlot = binding.targetIndex,
                     .textureAssetID = static_cast<FileFormat::AssetID>(overrideValue.value[0])});
            }
        }

        for (u32 slot = 0; slot < slotOverrides.size(); ++slot)
        {
            if (!slotOverrides[slot].empty())
                materials[slot] = _assets->DeriveMaterialInstance(materials[slot],
                    slotOverrides[slot], modelAssetID);
        }
        if (!scene.SetModelMaterials(instance, materials))
        {
            ++_failures;
            return DisplayApplyResult::SceneUpdateFailed;
        }

        ++_appliedSelections;
        return DisplayApplyResult::Applied;
    }

    DisplayResolverStats DisplayResolver::GetStats() const
    {
        return {.selections = static_cast<u32>(_ranges.size()),
                .assignments = static_cast<u32>(_overrides.size()),
                .applyRequests = _applyRequests,
                .appliedSelections = _appliedSelections,
                .missingSelections = _missingSelections,
                .failures = _failures};
    }
} // namespace ModelLoading
