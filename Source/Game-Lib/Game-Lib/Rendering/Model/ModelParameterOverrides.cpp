#include "ModelParameterOverrides.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <algorithm>
#include <vector>

namespace ModelLoading
{
    bool ModelParameterOverrides::SetTexture(RenderScenes::RenderScene& scene,
                                             RenderScenes::ModelInstanceHandle instance,
                                             RenderAssets::ModelHandle model, u64 parameterNameHash,
                                             Renderer::TextureID textureID)
    {
        if (!_assets || (!scene.IsAlive(instance) && !scene.IsPending(instance)))
            return false;

        const ModelGeometryStorage& geometry = _assets->GetModelGeometryStorage();
        const auto parameters = geometry.GetParameters(model);
        const auto parameter = std::find_if(parameters.begin(), parameters.end(), [parameterNameHash](const FileFormat::Model::Parameter& candidate) {
            return candidate.nameHash == parameterNameHash && candidate.type == FileFormat::Model::ParameterType::Texture2D;
        });
        if (parameter == parameters.end())
            return false;

        const ModelScene::ModelInstanceResources* resources = scene.GetModelInstances().GetResources(instance);
        if (!resources)
            return false;

        const ModelScene::ModelMaterialTableStore& tables = scene.GetModelMaterialTables();
        const u32 materialCount = tables.GetCount(resources->materialTable);
        std::vector<RenderAssets::MaterialInstanceHandle> materials;
        materials.reserve(materialCount);
        for (u32 slot = 0; slot < materialCount; ++slot)
            materials.emplace_back(tables.GetMaterial(resources->materialTable, slot));

        std::vector<std::vector<MaterialLoading::MaterialTextureRuntimeOverride>> slotOverrides(materialCount);
        for (const FileFormat::Model::ParameterBinding& binding : geometry.GetParameterBindings(model))
        {
            if (binding.parameterStableID != parameter->stableID || binding.target != FileFormat::Model::ParameterBindingTarget::TextureSlot)
                continue;

            u32 materialSlot = 0;
            if (!geometry.FindMaterialSlot(model, binding.materialSlotStableID, materialSlot) || materialSlot >= materialCount)
                return false;
            slotOverrides[materialSlot].push_back({.textureSlot = binding.targetIndex, .textureID = textureID});
        }

        bool applied = false;
        for (u32 slot = 0; slot < materialCount; ++slot)
        {
            if (slotOverrides[slot].empty())
                continue;
            materials[slot] = _assets->DeriveMaterialInstance(materials[slot], slotOverrides[slot]);
            applied = true;
        }
        return applied && scene.SetModelMaterials(instance, materials);
    }
} // namespace ModelLoading
