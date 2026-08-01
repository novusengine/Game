#include "ModelAssetValidator.h"

#include <cmath>

namespace
{
    using AssetLoading::Diagnostic;
    using AssetLoading::DiagnosticCode;
    namespace Model = FileFormat::Model;

    constexpr u32 MODEL_FLAGS = Model::ModelFlags_HasEmbeddedInstances;
    constexpr u32 MESH_FLAGS = Model::MeshFlags_Skinned;
    constexpr u32 LOD_FLAGS = Model::MeshLODFlags_HasSkinningData | Model::MeshLODFlags_StaticFallbackIsBindPose;

    bool IsRangeValid(u32 offset, u32 count, size_t size)
    {
        return offset <= size && count <= size - offset;
    }

    bool IsFinite(const vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool IsFinite(const quat& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    bool IsValidBounds(const Model::Bounds& bounds)
    {
        return IsFinite(bounds.center) && IsFinite(bounds.extents) && std::isfinite(bounds.sphereRadius) && bounds.sphereRadius >= 0.0f &&
               bounds.extents.x >= 0.0f && bounds.extents.y >= 0.0f && bounds.extents.z >= 0.0f;
    }

    ModelLoading::ModelAssetReadResult Fail(DiagnosticCode code, std::string_view field, u32 index = Diagnostic::NO_INDEX, u64 observed = 0, u64 expected = 0)
    {
        ModelLoading::ModelAssetReadResult result;
        result.diagnostic = {code, field, index, observed, expected};
        return result;
    }
} // namespace

namespace ModelLoading
{
    ModelAssetReadResult ModelAssetValidator::Validate(ModelAssetReadResult result)
    {
        const Model::ModelAsset& root = result.view.root;

        if ((root.flags & ~MODEL_FLAGS) != 0)
            return Fail(DiagnosticCode::UnsupportedFlags, "ModelAsset.flags", Diagnostic::NO_INDEX, root.flags, MODEL_FLAGS);
        if (!IsValidBounds(root.bounds))
            return Fail(DiagnosticCode::NonFiniteValue, "ModelAsset.bounds");
        if (root.bounds.reserved != 0)
            return Fail(DiagnosticCode::ReservedValueNonZero, "ModelAsset.bounds.reserved", Diagnostic::NO_INDEX, root.bounds.reserved, 0);
        if (root.collisionAssetID == FileFormat::INVALID_ASSET_ID)
            ++result.limitations.invalidCollisionReferences;

        for (u32 index = 0; index < result.view.positions.size(); ++index)
        {
            if (result.view.positions[index].reserved != 0)
                return Fail(DiagnosticCode::ReservedValueNonZero, "positions.reserved", index, result.view.positions[index].reserved, 0);
        }
        for (u32 index = 0; index < result.view.vertexAttributes.size(); ++index)
        {
            if ((result.view.vertexAttributes[index].tangent & 0x80000000u) != 0)
                return Fail(DiagnosticCode::ReservedValueNonZero, "vertexAttributes.tangent", index, result.view.vertexAttributes[index].tangent, 0x7FFFFFFFu);
        }
        for (u32 index = 0; index < result.view.materialSlots.size(); ++index)
        {
            const Model::MaterialSlot& slot = result.view.materialSlots[index];
            if (slot.defaultMaterialInstanceAssetID == FileFormat::INVALID_ASSET_ID)
                return Fail(DiagnosticCode::MissingRequiredReference, "materialSlots.defaultMaterialInstanceAssetID", index);
            if (slot.reserved != 0)
                return Fail(DiagnosticCode::ReservedValueNonZero, "materialSlots.reserved", index, slot.reserved, 0);
        }

        for (u32 meshIndex = 0; meshIndex < result.view.meshes.size(); ++meshIndex)
        {
            const Model::Mesh& mesh = result.view.meshes[meshIndex];
            if (!IsRangeValid(mesh.lodOffset, mesh.numLODs, result.view.meshLODs.size()))
                return Fail(DiagnosticCode::InvalidRange, "meshes.lods", meshIndex, mesh.lodOffset, mesh.numLODs);
            if (!IsRangeValid(mesh.materialSlotOffset, mesh.numMaterialSlots, result.view.materialSlots.size()))
                return Fail(DiagnosticCode::InvalidRange, "meshes.materialSlots", meshIndex, mesh.materialSlotOffset, mesh.numMaterialSlots);
            if (mesh.numLODs == 0)
                return Fail(DiagnosticCode::EmptyRequiredRange, "meshes.lods", meshIndex);
            if (mesh.numMaterialSlots == 0)
                return Fail(DiagnosticCode::EmptyRequiredRange, "meshes.materialSlots", meshIndex);
            if ((mesh.flags & ~MESH_FLAGS) != 0)
                return Fail(DiagnosticCode::UnsupportedFlags, "meshes.flags", meshIndex, mesh.flags, MESH_FLAGS);
            if (!IsValidBounds(mesh.bounds) || !IsFinite(mesh.positionDecodeOffset) || !IsFinite(mesh.positionDecodeExtent) ||
                mesh.positionDecodeExtent.x < 0.0f || mesh.positionDecodeExtent.y < 0.0f || mesh.positionDecodeExtent.z < 0.0f ||
                !std::isfinite(mesh.reserved0))
                return Fail(DiagnosticCode::NonFiniteValue, "meshes.boundsOrDecode", meshIndex);
            if (mesh.bounds.reserved != 0 || mesh.reserved0 != 0.0f)
                return Fail(DiagnosticCode::ReservedValueNonZero, "meshes.reserved", meshIndex);
            if (mesh.skeletonAssetID == FileFormat::INVALID_ASSET_ID)
                ++result.limitations.invalidSkeletonReferences;
            if (mesh.animationBoundsAssetID == FileFormat::INVALID_ASSET_ID)
                ++result.limitations.invalidAnimationBoundsReferences;

            f32 previousError = -1.0f;
            bool meshHasSkinning = false;
            for (u32 localLODIndex = 0; localLODIndex < mesh.numLODs; ++localLODIndex)
            {
                const u32 lodIndex = mesh.lodOffset + localLODIndex;
                const Model::MeshLOD& lod = result.view.meshLODs[lodIndex];
                if (!IsRangeValid(lod.vertexOffset, lod.numVertices, result.view.positions.size()) ||
                    !IsRangeValid(lod.vertexAttributeOffset, lod.numVertexAttributes, result.view.vertexAttributes.size()) ||
                    !IsRangeValid(lod.skinningDataOffset, lod.numSkinningData, result.view.skinningData.size()) ||
                    !IsRangeValid(lod.submeshOffset, lod.numSubmeshes, result.view.submeshes.size()) ||
                    !IsRangeValid(lod.meshletOffset, lod.numMeshlets, result.view.meshlets.size()) ||
                    !IsRangeValid(lod.jointPaletteRemapOffset, lod.numJointPaletteRemaps, result.view.jointPaletteRemaps.size()))
                    return Fail(DiagnosticCode::InvalidRange, "meshLODs.streams", lodIndex);
                if (lod.numVertices == 0 || lod.numSubmeshes == 0 || lod.numMeshlets == 0)
                    return Fail(DiagnosticCode::EmptyRequiredRange, "meshLODs.geometry", lodIndex);
                if (lod.numVertexAttributes != lod.numVertices)
                    return Fail(DiagnosticCode::CountMismatch, "meshLODs.vertexAttributes", lodIndex, lod.numVertexAttributes, lod.numVertices);
                if (lod.numSkinningData != 0 && lod.numSkinningData != lod.numVertices)
                    return Fail(DiagnosticCode::CountMismatch, "meshLODs.skinningData", lodIndex, lod.numSkinningData, lod.numVertices);
                if (lod.numJointPaletteRemaps > 256)
                    return Fail(DiagnosticCode::InvalidValue, "meshLODs.numJointPaletteRemaps", lodIndex, lod.numJointPaletteRemaps, 256);
                if ((lod.flags & ~LOD_FLAGS) != 0)
                    return Fail(DiagnosticCode::UnsupportedFlags, "meshLODs.flags", lodIndex, lod.flags, LOD_FLAGS);
                if (!IsValidBounds(lod.bounds) || !std::isfinite(lod.geometricError) || lod.geometricError < 0.0f)
                    return Fail(DiagnosticCode::NonFiniteValue, "meshLODs.boundsOrError", lodIndex);
                if (lod.bounds.reserved != 0)
                    return Fail(DiagnosticCode::ReservedValueNonZero, "meshLODs.bounds.reserved", lodIndex, lod.bounds.reserved, 0);
                if (lod.geometricError < previousError)
                    return Fail(DiagnosticCode::InvalidValue, "meshLODs.geometricError", lodIndex);
                previousError = lod.geometricError;

                const bool hasSkinning = lod.numSkinningData != 0;
                meshHasSkinning |= hasSkinning;
                if (hasSkinning != ((lod.flags & Model::MeshLODFlags_HasSkinningData) != 0))
                    return Fail(DiagnosticCode::CountMismatch, "meshLODs.hasSkinningData", lodIndex, lod.flags, lod.numSkinningData);
                if (hasSkinning && (lod.flags & Model::MeshLODFlags_StaticFallbackIsBindPose) == 0)
                    return Fail(DiagnosticCode::UnsupportedFlags, "meshLODs.staticFallback", lodIndex, lod.flags, Model::MeshLODFlags_StaticFallbackIsBindPose);
                if (!hasSkinning && (lod.flags & Model::MeshLODFlags_StaticFallbackIsBindPose) != 0)
                    return Fail(DiagnosticCode::UnsupportedFlags, "meshLODs.staticFallback", lodIndex, lod.flags, 0);

                for (u32 skinIndex = 0; skinIndex < lod.numSkinningData; ++skinIndex)
                {
                    const Model::PackedSkinningData& skinning = result.view.skinningData[lod.skinningDataOffset + skinIndex];
                    u32 weightSum = 0;
                    for (u32 influence = 0; influence < 4; ++influence)
                    {
                        const u32 weight = (skinning.jointWeights >> (influence * 8u)) & 0xFFu;
                        const u32 joint = (skinning.jointIndices >> (influence * 8u)) & 0xFFu;
                        weightSum += weight;
                        if (weight > 0 && joint >= lod.numJointPaletteRemaps)
                            return Fail(DiagnosticCode::InvalidIndex, "skinningData.jointIndices", lod.skinningDataOffset + skinIndex, joint,
                                        lod.numJointPaletteRemaps);
                    }
                    if (weightSum != 255)
                        return Fail(DiagnosticCode::InvalidValue, "skinningData.jointWeights", lod.skinningDataOffset + skinIndex, weightSum, 255);
                }

                u32 expectedMeshletOffset = lod.meshletOffset;
                for (u32 localSubmeshIndex = 0; localSubmeshIndex < lod.numSubmeshes; ++localSubmeshIndex)
                {
                    const u32 submeshIndex = lod.submeshOffset + localSubmeshIndex;
                    const Model::Submesh& submesh = result.view.submeshes[submeshIndex];
                    if (submesh.meshletOffset != expectedMeshletOffset || submesh.numMeshlets == 0 ||
                        !IsRangeValid(submesh.meshletOffset, submesh.numMeshlets, result.view.meshlets.size()) || submesh.meshletOffset < lod.meshletOffset ||
                        static_cast<u64>(submesh.meshletOffset) + submesh.numMeshlets > static_cast<u64>(lod.meshletOffset) + lod.numMeshlets)
                        return Fail(DiagnosticCode::InvalidRange, "submeshes.meshlets", submeshIndex, submesh.meshletOffset, submesh.numMeshlets);
                    if (submesh.materialSlotIndex >= mesh.numMaterialSlots)
                        return Fail(DiagnosticCode::InvalidIndex, "submeshes.materialSlotIndex", submeshIndex, submesh.materialSlotIndex,
                                    mesh.numMaterialSlots);
                    if (submesh.geometryGroupID >= root.geometryGroupCount)
                        return Fail(DiagnosticCode::InvalidIndex, "submeshes.geometryGroupID", submeshIndex, submesh.geometryGroupID, root.geometryGroupCount);
                    if (submesh.flags != Model::SubmeshFlags_None)
                        return Fail(DiagnosticCode::UnsupportedFlags, "submeshes.flags", submeshIndex, submesh.flags, 0);
                    expectedMeshletOffset += submesh.numMeshlets;

                    for (u32 meshletIndex = submesh.meshletOffset; meshletIndex < submesh.meshletOffset + submesh.numMeshlets; ++meshletIndex)
                    {
                        const Model::Meshlet& meshlet = result.view.meshlets[meshletIndex];
                        if (meshlet.vertexCount == 0 || meshlet.vertexCount > Model::MAX_MESHLET_VERTICES)
                            return Fail(DiagnosticCode::InvalidValue, "meshlets.vertexCount", meshletIndex, meshlet.vertexCount, Model::MAX_MESHLET_VERTICES);
                        if (meshlet.triangleCount == 0 || meshlet.triangleCount > Model::MAX_MESHLET_TRIANGLES)
                            return Fail(DiagnosticCode::InvalidValue, "meshlets.triangleCount", meshletIndex, meshlet.triangleCount,
                                        Model::MAX_MESHLET_TRIANGLES);
                        if (!IsRangeValid(meshlet.vertexOffset, meshlet.vertexCount, result.view.meshletVertexIndices.size()) ||
                            !IsRangeValid(meshlet.triangleOffset, meshlet.triangleCount, result.view.meshletTriangles.size()))
                            return Fail(DiagnosticCode::InvalidRange, "meshlets.streams", meshletIndex);
                        if (!IsFinite(meshlet.boundsCenter) || !std::isfinite(meshlet.boundsRadius) || meshlet.boundsRadius < 0.0f)
                            return Fail(DiagnosticCode::NonFiniteValue, "meshlets.bounds", meshletIndex);
                        for (u32 vertexIndex = 0; vertexIndex < meshlet.vertexCount; ++vertexIndex)
                        {
                            const u32 vertex = result.view.meshletVertexIndices[meshlet.vertexOffset + vertexIndex];
                            if (vertex >= lod.numVertices)
                                return Fail(DiagnosticCode::InvalidIndex, "meshletVertexIndices", meshlet.vertexOffset + vertexIndex, vertex, lod.numVertices);
                        }
                        for (u32 triangleIndex = 0; triangleIndex < meshlet.triangleCount; ++triangleIndex)
                        {
                            const u32 triangle = result.view.meshletTriangles[meshlet.triangleOffset + triangleIndex].localVertexIndices;
                            if ((triangle & 0xFF000000u) != 0 || (triangle & 0xFFu) >= meshlet.vertexCount ||
                                ((triangle >> 8u) & 0xFFu) >= meshlet.vertexCount || ((triangle >> 16u) & 0xFFu) >= meshlet.vertexCount)
                                return Fail(DiagnosticCode::InvalidIndex, "meshletTriangles", meshlet.triangleOffset + triangleIndex, triangle,
                                            meshlet.vertexCount);
                        }
                    }
                }
                if (expectedMeshletOffset != lod.meshletOffset + lod.numMeshlets)
                    return Fail(DiagnosticCode::InvalidRange, "meshLODs.meshletCoverage", lodIndex, expectedMeshletOffset, lod.meshletOffset + lod.numMeshlets);
            }

            if (meshHasSkinning != ((mesh.flags & Model::MeshFlags_Skinned) != 0))
                return Fail(DiagnosticCode::CountMismatch, "meshes.skinned", meshIndex, mesh.flags, meshHasSkinning);
        }

        const bool hasEmbeddedInstances = !result.view.embeddedInstanceSets.empty() || !result.view.embeddedInstances.empty();
        if (hasEmbeddedInstances != ((root.flags & Model::ModelFlags_HasEmbeddedInstances) != 0))
            return Fail(DiagnosticCode::CountMismatch, "ModelAsset.hasEmbeddedInstances", Diagnostic::NO_INDEX, root.flags, hasEmbeddedInstances);
        for (u32 index = 0; index < result.view.embeddedInstanceSets.size(); ++index)
        {
            const Model::EmbeddedInstanceSet& set = result.view.embeddedInstanceSets[index];
            if (!IsRangeValid(set.instanceOffset, set.numInstances, result.view.embeddedInstances.size()))
                return Fail(DiagnosticCode::InvalidRange, "embeddedInstanceSets.instances", index, set.instanceOffset, set.numInstances);
            if (set.reserved != 0)
                return Fail(DiagnosticCode::ReservedValueNonZero, "embeddedInstanceSets.reserved", index, set.reserved, 0);
        }
        for (u32 index = 0; index < result.view.embeddedInstances.size(); ++index)
        {
            const Model::EmbeddedInstance& instance = result.view.embeddedInstances[index];
            if (!IsFinite(instance.position) || !IsFinite(instance.rotation) || !std::isfinite(instance.uniformScale) || instance.uniformScale <= 0.0f)
                return Fail(DiagnosticCode::NonFiniteValue, "embeddedInstances.transform", index);
            if (instance.reserved != 0)
                return Fail(DiagnosticCode::ReservedValueNonZero, "embeddedInstances.reserved", index, instance.reserved, 0);
            if (instance.modelAssetID == FileFormat::INVALID_ASSET_ID)
                ++result.limitations.invalidEmbeddedModelReferences;
        }

        return result;
    }
} // namespace ModelLoading
