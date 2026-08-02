#include "ModelDiagnosticWorkBuilder.h"

#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

namespace
{
    u32 MixColorSeed(u32 seed, u32 value)
    {
        seed ^= value + 0x9E3779B9u + (seed << 6u) + (seed >> 2u);
        seed ^= seed >> 16u;
        seed *= 0x7FEB352Du;
        seed ^= seed >> 15u;
        return seed;
    }
}

namespace ModelPipeline
{
    DiagnosticWorkBuildResult ModelDiagnosticWorkBuilder::Build(
        const RenderScenes::RenderScene& scene, const ModelLoading::ModelGeometryStorage& geometry,
        const MaterialLoading::MaterialStorage& materials,
        std::span<const RenderScenes::ModelInstanceHandle> selection)
    {
        DiagnosticWorkBuildResult result;
        result.stats.selectedInstances = static_cast<u32>(selection.size());

        for (const RenderScenes::ModelInstanceHandle instance : selection)
        {
            const ModelScene::ModelInstanceGPURecord* instanceRecord = scene.GetModelInstance(instance);
            const ModelScene::ModelInstanceResources* instanceResources = scene.GetModelInstances().GetResources(instance);
            if (!scene.IsAlive(instance) || !instanceRecord || !instanceResources ||
                (instanceRecord->flags & ModelScene::ModelInstanceFlagVisible) == 0)
            {
                ++result.stats.skippedInvisibleInstances;
                continue;
            }

            const RenderAssets::ModelHandle modelHandle(instanceRecord->modelIndex);
            if (!geometry.HasModel(modelHandle))
                continue;

            const ModelLoading::ModelGPURecord& modelRecord = geometry.GetRecord(modelHandle);
            for (u32 meshIndex = 0; meshIndex < modelRecord.numMeshes; ++meshIndex)
            {
                const FileFormat::Model::Mesh& mesh = geometry.GetMeshes()[modelRecord.meshBase + meshIndex];
                if (mesh.numLODs == 0)
                    continue;

                const u32 lodIndex = mesh.lodOffset;
                const FileFormat::Model::MeshLOD& lod = geometry.GetMeshLODs()[modelRecord.meshLODBase + lodIndex];
                if ((lod.flags & FileFormat::Model::MeshLODFlags_HasSkinningData) != 0 &&
                    (lod.flags & FileFormat::Model::MeshLODFlags_StaticFallbackIsBindPose) == 0)
                {
                    ++result.stats.skippedSkinnedLODs;
                    continue;
                }

                for (u32 submeshIndex = 0; submeshIndex < lod.numSubmeshes; ++submeshIndex)
                {
                    const FileFormat::Model::Submesh& submesh =
                        geometry.GetSubmeshes()[modelRecord.submeshBase + lod.submeshOffset + submeshIndex];
                    if (!scene.GetGeometryGroupMasks().IsEnabled(instanceResources->geometryGroupMask,
                                                                  submesh.geometryGroupID))
                    {
                        ++result.stats.skippedGeometryGroups;
                        continue;
                    }

                    bool twoSided = true;
                    const ModelScene::ModelMaterialTableStore& tables = scene.GetModelMaterialTables();
                    if (submesh.materialSlotIndex < tables.GetCount(instanceResources->materialTable))
                    {
                        const RenderAssets::MaterialInstanceHandle materialInstance(
                            tables.GetMaterial(instanceResources->materialTable, submesh.materialSlotIndex));
                        if (materials.HasMaterialInstance(materialInstance))
                        {
                            const MaterialLoading::MaterialInstanceGPURecord& materialInstanceRecord =
                                materials.GetMaterialInstance(materialInstance);
                            const RenderAssets::MaterialHandle material(materialInstanceRecord.materialIndex);
                            if (materials.HasMaterial(material))
                            {
                                twoSided = (materials.GetMaterial(material).flags &
                                            FileFormat::Material::MaterialFlags_TwoSided) != 0;
                            }
                        }
                    }

                    std::vector<ModelView::DiagnosticMeshletWork>& destination =
                        twoSided ? result.twoSided : result.oneSided;
                    for (u32 submeshMeshletIndex = 0; submeshMeshletIndex < submesh.numMeshlets;
                         ++submeshMeshletIndex)
                    {
                        ModelView::DiagnosticMeshletWork work;
                        work.instanceIndex = RenderScenes::GetModelInstanceSlot(instance);
                        work.meshletIndex = modelRecord.meshletBase + submesh.meshletOffset + submeshMeshletIndex;
                        work.positionBase = modelRecord.positionBase + lod.vertexOffset;
                        work.vertexAttributeBase = modelRecord.vertexAttributeBase + lod.vertexAttributeOffset;
                        work.meshletVertexIndexBase = modelRecord.meshletVertexIndexBase;
                        work.meshletTriangleBase = modelRecord.meshletTriangleBase;

                        u32 colorSeed = MixColorSeed(0xA511E9B3u, instanceRecord->modelIndex);
                        colorSeed = MixColorSeed(colorSeed, meshIndex);
                        colorSeed = MixColorSeed(colorSeed, lodIndex);
                        colorSeed = MixColorSeed(colorSeed, submeshIndex);
                        work.colorSeed = MixColorSeed(colorSeed, submeshMeshletIndex);
                        work.positionDecodeOffset = vec4(mesh.positionDecodeOffset, 0.0f);
                        work.positionDecodeExtent = vec4(mesh.positionDecodeExtent, 0.0f);
                        destination.push_back(work);
                    }
                }
            }
        }

        result.stats.oneSidedMeshlets = static_cast<u32>(result.oneSided.size());
        result.stats.twoSidedMeshlets = static_cast<u32>(result.twoSided.size());
        return result;
    }
} // namespace ModelPipeline
