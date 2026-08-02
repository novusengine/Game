#include "MaterialAssetReader.h"
#include "MaterialAssetValidator.h"

#include <Base/Memory/Bytebuffer.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

namespace
{
    using AssetLoading::Diagnostic;
    using AssetLoading::DiagnosticCode;
    namespace Material = FileFormat::Material;

    template <typename T>
    bool IsRootSectionValid(std::span<const u8> payload, u32 offset, u32 count, size_t rootSize)
    {
        if (count == 0)
            return offset == 0;
        if (offset < rootSize || offset % 16 != 0 || count > std::numeric_limits<size_t>::max() / sizeof(T))
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

    template <typename T>
    MaterialLoading::MaterialAssetReadResult<T> Fail(DiagnosticCode code, std::string_view field, u32 index = Diagnostic::NO_INDEX, u64 observed = 0,
                                                     u64 expected = 0)
    {
        MaterialLoading::MaterialAssetReadResult<T> result;
        result.diagnostic = {code, field, index, observed, expected};
        return result;
    }
} // namespace

namespace MaterialLoading
{
    MaterialAssetReadResult<MaterialAssetView> MaterialAssetReader::ReadMaterial(std::span<const u8> payload,
                                                                                 AssetLoading::ValidationMode validationMode)
    {
        if (payload.size() < sizeof(Material::MaterialAsset))
            return Fail<MaterialAssetView>(DiagnosticCode::PayloadTooSmall, "MaterialAsset", Diagnostic::NO_INDEX, payload.size(),
                                           sizeof(Material::MaterialAsset));

        Material::MaterialAsset root;
        std::memcpy(&root, payload.data(), sizeof(root));
        if (root.header.type != Material::MATERIAL_FILE_TYPE || root.header.version != Material::DEVELOPMENT_VERSION)
            return Fail<MaterialAssetView>(DiagnosticCode::InvalidHeader, "MaterialAsset.header");
        if (!IsRootSectionValid<Material::ParameterDefinition>(payload, root.parametersOffset, root.numParameters, sizeof(root)))
            return Fail<MaterialAssetView>(DiagnosticCode::InvalidRootSection, "parameters", Diagnostic::NO_INDEX, root.parametersOffset, root.numParameters);
        if (!IsRootSectionValid<u8>(payload, root.defaultParameterDataOffset, root.defaultParameterDataSize, sizeof(root)))
            return Fail<MaterialAssetView>(DiagnosticCode::InvalidRootSection, "defaultParameterData", Diagnostic::NO_INDEX, root.defaultParameterDataOffset,
                                           root.defaultParameterDataSize);

        std::shared_ptr<Bytebuffer> buffer = std::make_shared<Bytebuffer>(const_cast<u8*>(payload.data()), payload.size());
        buffer->writtenData = payload.size();
        Material::MaterialAsset parsedRoot;
        if (!Material::MaterialAsset::Read(buffer, parsedRoot))
            return Fail<MaterialAssetView>(DiagnosticCode::InvalidRootSection, "MaterialAsset");

        MaterialAssetReadResult<MaterialAssetView> result;
        result.view.root = parsedRoot;
        result.view.parameters = GetRootSection<Material::ParameterDefinition>(payload, root.parametersOffset, root.numParameters);
        result.view.defaultParameterData = GetRootSection<u8>(payload, root.defaultParameterDataOffset, root.defaultParameterDataSize);

        if (AssetLoading::ShouldPerformFullValidation(validationMode))
            return MaterialAssetValidator::ValidateMaterial(result);

        return result;
    }

    MaterialAssetReadResult<MaterialInstanceAssetView> MaterialAssetReader::DecodeMaterialInstance(std::span<const u8> payload)
    {
        if (payload.size() < sizeof(Material::MaterialInstanceAsset))
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::PayloadTooSmall, "MaterialInstanceAsset", Diagnostic::NO_INDEX, payload.size(),
                                                   sizeof(Material::MaterialInstanceAsset));

        Material::MaterialInstanceAsset root;
        std::memcpy(&root, payload.data(), sizeof(root));
        if (root.header.type != Material::MATERIAL_INSTANCE_FILE_TYPE || root.header.version != Material::DEVELOPMENT_VERSION)
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidHeader, "MaterialInstanceAsset.header");
        if (!IsRootSectionValid<u8>(payload, root.parameterDataOffset, root.parameterDataSize, sizeof(root)))
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidRootSection, "parameterData", Diagnostic::NO_INDEX, root.parameterDataOffset,
                                                   root.parameterDataSize);
        if (!IsRootSectionValid<Material::ResourceBinding>(payload, root.resourceBindingsOffset, root.numResourceBindings, sizeof(root)))
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidRootSection, "resourceBindings", Diagnostic::NO_INDEX, root.resourceBindingsOffset,
                                                   root.numResourceBindings);
        if (!IsRootSectionValid<Material::MaterialAnimationBinding>(payload, root.animationBindingsOffset, root.numAnimationBindings, sizeof(root)))
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidRootSection, "animationBindings", Diagnostic::NO_INDEX, root.animationBindingsOffset,
                                                   root.numAnimationBindings);

        std::shared_ptr<Bytebuffer> buffer = std::make_shared<Bytebuffer>(const_cast<u8*>(payload.data()), payload.size());
        buffer->writtenData = payload.size();
        Material::MaterialInstanceAsset parsedRoot;
        if (!Material::MaterialInstanceAsset::Read(buffer, parsedRoot))
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidRootSection, "MaterialInstanceAsset");

        MaterialAssetReadResult<MaterialInstanceAssetView> result;
        result.view.root = parsedRoot;
        result.view.parameterData = GetRootSection<u8>(payload, root.parameterDataOffset, root.parameterDataSize);
        result.view.resourceBindings = GetRootSection<Material::ResourceBinding>(payload, root.resourceBindingsOffset, root.numResourceBindings);
        result.view.animationBindings = GetRootSection<Material::MaterialAnimationBinding>(payload, root.animationBindingsOffset, root.numAnimationBindings);

        return result;
    }

    MaterialAssetReadResult<MaterialInstanceAssetView> MaterialAssetReader::ReadMaterialInstance(
        std::span<const u8> payload, const MaterialAssetView& material, AssetLoading::ValidationMode validationMode)
    {
        MaterialAssetReadResult<MaterialInstanceAssetView> result = DecodeMaterialInstance(payload);
        if (!result || !AssetLoading::ShouldPerformFullValidation(validationMode))
            return result;

        return MaterialAssetValidator::ValidateMaterialInstance(result, material);
    }
} // namespace MaterialLoading
