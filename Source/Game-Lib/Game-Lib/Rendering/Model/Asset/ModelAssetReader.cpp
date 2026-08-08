#include "ModelAssetReader.h"
#include "ModelAssetValidator.h"

#include <Base/Memory/Bytebuffer.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

namespace
{
    using AssetLoading::Diagnostic;
    using AssetLoading::DiagnosticCode;
    namespace Model = FileFormat::Model;

    template <typename T>
    bool IsRootSectionValid(std::span<const u8> payload, u32 offset, u32 count)
    {
        if (count == 0)
            return offset == 0;
        if (offset < sizeof(Model::ModelAsset) || offset % 16 != 0 || count > std::numeric_limits<size_t>::max() / sizeof(T))
            return false;

        const size_t byteCount = static_cast<size_t>(count) * sizeof(T);
        return offset <= payload.size() && byteCount <= payload.size() - offset && (reinterpret_cast<uintptr_t>(payload.data() + offset) % alignof(T)) == 0;
    }

    template <typename T>
    std::span<const T> GetRootSection(std::span<const u8> payload, u32 offset, u32 count)
    {
        if (count == 0)
            return {};
        return {reinterpret_cast<const T*>(payload.data() + offset), count};
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
    ModelAssetReadResult ModelAssetReader::Read(std::span<const u8> payload, AssetLoading::ValidationMode validationMode)
    {
        if (payload.size() < sizeof(Model::ModelAsset))
            return Fail(DiagnosticCode::PayloadTooSmall, "ModelAsset", Diagnostic::NO_INDEX, payload.size(), sizeof(Model::ModelAsset));

        Model::ModelAsset root;
        std::memcpy(&root, payload.data(), sizeof(root));
        if (root.header.type != Model::FILE_TYPE || root.header.version != Model::VERSION)
            return Fail(DiagnosticCode::InvalidHeader, "ModelAsset.header");

#define VALIDATE_ROOT_SECTION(member, countMember, type)                           \
    if (!IsRootSectionValid<type>(payload, root.member##Offset, root.countMember)) \
    return Fail(DiagnosticCode::InvalidRootSection, #member, Diagnostic::NO_INDEX, root.member##Offset, root.countMember)

        VALIDATE_ROOT_SECTION(meshes, numMeshes, Model::Mesh);
        VALIDATE_ROOT_SECTION(meshLODs, numMeshLODs, Model::MeshLOD);
        VALIDATE_ROOT_SECTION(submeshes, numSubmeshes, Model::Submesh);
        VALIDATE_ROOT_SECTION(meshlets, numMeshlets, Model::Meshlet);
        VALIDATE_ROOT_SECTION(positions, numPositions, Model::PackedPosition);
        VALIDATE_ROOT_SECTION(vertexAttributes, numVertexAttributes, Model::PackedVertexAttributes);
        VALIDATE_ROOT_SECTION(skinningData, numSkinningData, Model::PackedSkinningData);
        VALIDATE_ROOT_SECTION(meshletVertexIndices, numMeshletVertexIndices, u32);
        VALIDATE_ROOT_SECTION(meshletTriangles, numMeshletTriangles, Model::PackedMeshletTriangle);
        VALIDATE_ROOT_SECTION(jointPaletteRemaps, numJointPaletteRemaps, u16);
        VALIDATE_ROOT_SECTION(materialSlots, numMaterialSlots, Model::MaterialSlot);
        VALIDATE_ROOT_SECTION(parameters, numParameters, Model::Parameter);
        VALIDATE_ROOT_SECTION(parameterBindings, numParameterBindings, Model::ParameterBinding);
        VALIDATE_ROOT_SECTION(embeddedInstanceSets, numEmbeddedInstanceSets, Model::EmbeddedInstanceSet);
        VALIDATE_ROOT_SECTION(embeddedInstances, numEmbeddedInstances, Model::EmbeddedInstance);
#undef VALIDATE_ROOT_SECTION

        std::shared_ptr<Bytebuffer> buffer = std::make_shared<Bytebuffer>(const_cast<u8*>(payload.data()), payload.size());
        buffer->writtenData = payload.size();
        Model::ModelAsset parsedRoot;
        if (!Model::ModelAsset::Read(buffer, parsedRoot))
            return Fail(DiagnosticCode::InvalidRootSection, "ModelAsset");

        ModelAssetReadResult result;
        result.view.root = parsedRoot;
#define SET_ROOT_SECTION(member, countMember, type) result.view.member = GetRootSection<type>(payload, root.member##Offset, root.countMember)
        SET_ROOT_SECTION(meshes, numMeshes, Model::Mesh);
        SET_ROOT_SECTION(meshLODs, numMeshLODs, Model::MeshLOD);
        SET_ROOT_SECTION(submeshes, numSubmeshes, Model::Submesh);
        SET_ROOT_SECTION(meshlets, numMeshlets, Model::Meshlet);
        SET_ROOT_SECTION(positions, numPositions, Model::PackedPosition);
        SET_ROOT_SECTION(vertexAttributes, numVertexAttributes, Model::PackedVertexAttributes);
        SET_ROOT_SECTION(skinningData, numSkinningData, Model::PackedSkinningData);
        SET_ROOT_SECTION(meshletVertexIndices, numMeshletVertexIndices, u32);
        SET_ROOT_SECTION(meshletTriangles, numMeshletTriangles, Model::PackedMeshletTriangle);
        SET_ROOT_SECTION(jointPaletteRemaps, numJointPaletteRemaps, u16);
        SET_ROOT_SECTION(materialSlots, numMaterialSlots, Model::MaterialSlot);
        SET_ROOT_SECTION(parameters, numParameters, Model::Parameter);
        SET_ROOT_SECTION(parameterBindings, numParameterBindings, Model::ParameterBinding);
        SET_ROOT_SECTION(embeddedInstanceSets, numEmbeddedInstanceSets, Model::EmbeddedInstanceSet);
        SET_ROOT_SECTION(embeddedInstances, numEmbeddedInstances, Model::EmbeddedInstance);
#undef SET_ROOT_SECTION

        if (AssetLoading::ShouldPerformFullValidation(validationMode))
            return ModelAssetValidator::Validate(result);

        return result;
    }
} // namespace ModelLoading
