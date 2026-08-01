#include "MaterialAssetValidator.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace
{
    using AssetLoading::Diagnostic;
    using AssetLoading::DiagnosticCode;
    namespace Material = FileFormat::Material;

    constexpr u32 MATERIAL_FLAGS = Material::MaterialFlags_TwoSided | Material::MaterialFlags_CastsShadows | Material::MaterialFlags_ReceivesDecals |
                                   Material::MaterialFlags_ReceivesFog | Material::MaterialFlags_HasCoverageFunction;
    constexpr u8 RESOURCE_FLAGS = Material::ResourceBindingFlags_Optional;
    constexpr u8 ANIMATION_FLAGS = Material::MaterialAnimationBindingFlags_Looping;

    bool IsPowerOfTwo(u32 value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    u32 ParameterElementSize(Material::ParameterType type)
    {
        switch (type)
        {
        case Material::ParameterType::Float:
        case Material::ParameterType::UInt:
        case Material::ParameterType::Texture2D:
        case Material::ParameterType::TextureCube:
        case Material::ParameterType::Sampler:
            return 4;
        case Material::ParameterType::Float2:
        case Material::ParameterType::UInt2:
            return 8;
        case Material::ParameterType::Float3:
        case Material::ParameterType::UInt3:
            return 12;
        case Material::ParameterType::Float4:
        case Material::ParameterType::UInt4:
            return 16;
        }
        return 0;
    }

    bool IsResourceParameter(Material::ParameterType parameterType, Material::ResourceType resourceType)
    {
        switch (resourceType)
        {
        case Material::ResourceType::Texture2D:
            return parameterType == Material::ParameterType::Texture2D;
        case Material::ResourceType::TextureCube:
            return parameterType == Material::ParameterType::TextureCube;
        case Material::ResourceType::Sampler:
            return parameterType == Material::ParameterType::Sampler;
        }
        return false;
    }

    const Material::ParameterDefinition* FindParameter(std::span<const Material::ParameterDefinition> parameters, u32 byteOffset)
    {
        const auto it = std::find_if(parameters.begin(), parameters.end(), [byteOffset](const Material::ParameterDefinition& parameter) {
            return byteOffset >= parameter.byteOffset &&
                   static_cast<u64>(byteOffset) + sizeof(u32) <= static_cast<u64>(parameter.byteOffset) + parameter.byteSize;
        });
        return it == parameters.end() ? nullptr : &*it;
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
    MaterialAssetReadResult<MaterialAssetView> MaterialAssetValidator::ValidateMaterial(MaterialAssetReadResult<MaterialAssetView> result)
    {
        const Material::MaterialAsset& root = result.view.root;

        if (root.rasterClass > Material::RasterClass::Transparent)
            return Fail<MaterialAssetView>(DiagnosticCode::UnsupportedEnum, "MaterialAsset.rasterClass", Diagnostic::NO_INDEX,
                                           static_cast<u8>(root.rasterClass), static_cast<u8>(Material::RasterClass::Transparent));
        if (root.reserved0[0] != 0 || root.reserved0[1] != 0 || root.reserved0[2] != 0)
            return Fail<MaterialAssetView>(DiagnosticCode::ReservedValueNonZero, "MaterialAsset.reserved0");
        if ((root.flags & ~MATERIAL_FLAGS) != 0)
            return Fail<MaterialAssetView>(DiagnosticCode::UnsupportedFlags, "MaterialAsset.flags", Diagnostic::NO_INDEX, root.flags, MATERIAL_FLAGS);
        if (!IsPowerOfTwo(root.parameterBlockAlignment) || root.parameterBlockAlignment < alignof(u32) ||
            root.parameterBlockSize % root.parameterBlockAlignment != 0)
            return Fail<MaterialAssetView>(DiagnosticCode::InvalidValue, "MaterialAsset.parameterBlockAlignment", Diagnostic::NO_INDEX,
                                           root.parameterBlockAlignment, root.parameterBlockSize);
        if (root.parameterBlockSize != root.defaultParameterDataSize)
            return Fail<MaterialAssetView>(DiagnosticCode::CountMismatch, "MaterialAsset.defaultParameterDataSize", Diagnostic::NO_INDEX,
                                           root.defaultParameterDataSize, root.parameterBlockSize);

        std::unordered_set<u64> parameterNames;
        std::vector<bool> occupiedBytes(root.parameterBlockSize, false);
        for (u32 index = 0; index < result.view.parameters.size(); ++index)
        {
            const Material::ParameterDefinition& parameter = result.view.parameters[index];
            if (parameter.type > Material::ParameterType::Sampler)
                return Fail<MaterialAssetView>(DiagnosticCode::UnsupportedEnum, "parameters.type", index, static_cast<u8>(parameter.type),
                                               static_cast<u8>(Material::ParameterType::Sampler));
            const u32 elementSize = ParameterElementSize(parameter.type);
            if (parameter.nameHash == 0 || parameter.arrayCount == 0 || parameter.byteSize != elementSize * parameter.arrayCount ||
                parameter.byteOffset % alignof(u32) != 0 || parameter.byteOffset > root.parameterBlockSize ||
                parameter.byteSize > root.parameterBlockSize - parameter.byteOffset)
                return Fail<MaterialAssetView>(DiagnosticCode::InvalidValue, "parameters.layout", index, parameter.byteSize,
                                               elementSize * parameter.arrayCount);
            if (!parameterNames.insert(parameter.nameHash).second)
                return Fail<MaterialAssetView>(DiagnosticCode::DuplicateParameter, "parameters.nameHash", index, parameter.nameHash);
            for (u32 byteIndex = parameter.byteOffset; byteIndex < parameter.byteOffset + parameter.byteSize; ++byteIndex)
            {
                if (occupiedBytes[byteIndex])
                    return Fail<MaterialAssetView>(DiagnosticCode::OverlappingParameters, "parameters.byteOffset", index, byteIndex);
                occupiedBytes[byteIndex] = true;
            }
        }

        return result;
    }

    MaterialAssetReadResult<MaterialInstanceAssetView> MaterialAssetValidator::ValidateMaterialInstance(
        MaterialAssetReadResult<MaterialInstanceAssetView> result, const MaterialAssetView& material)
    {
        const Material::MaterialInstanceAsset& root = result.view.root;

        if (root.materialAssetID == FileFormat::INVALID_ASSET_ID)
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::MissingRequiredReference, "MaterialInstanceAsset.materialAssetID");
        if (root.parameterDataSize != material.root.parameterBlockSize)
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::CountMismatch, "MaterialInstanceAsset.parameterDataSize", Diagnostic::NO_INDEX,
                                                   root.parameterDataSize, material.root.parameterBlockSize);
        const u64 parameterLayoutHash = Material::CalculateParameterLayoutHash(material.parameters, material.root.parameterBlockSize);
        if (root.parameterLayoutHash != parameterLayoutHash)
            return Fail<MaterialInstanceAssetView>(DiagnosticCode::LayoutHashMismatch, "MaterialInstanceAsset.parameterLayoutHash", Diagnostic::NO_INDEX,
                                                   root.parameterLayoutHash, parameterLayoutHash);

        for (u32 index = 0; index < result.view.resourceBindings.size(); ++index)
        {
            const Material::ResourceBinding& binding = result.view.resourceBindings[index];
            if (binding.type > Material::ResourceType::Sampler)
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::UnsupportedEnum, "resourceBindings.type", index, static_cast<u8>(binding.type),
                                                       static_cast<u8>(Material::ResourceType::Sampler));
            if ((binding.flags & ~RESOURCE_FLAGS) != 0)
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::UnsupportedFlags, "resourceBindings.flags", index, binding.flags, RESOURCE_FLAGS);
            if (binding.parameterByteOffset % alignof(u32) != 0 || binding.parameterByteOffset > root.parameterDataSize ||
                sizeof(u32) > root.parameterDataSize - binding.parameterByteOffset)
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidRange, "resourceBindings.parameterByteOffset", index, binding.parameterByteOffset,
                                                       root.parameterDataSize);
            const Material::ParameterDefinition* parameter = FindParameter(material.parameters, binding.parameterByteOffset);
            if (!parameter || !IsResourceParameter(parameter->type, binding.type))
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidValue, "resourceBindings.parameterType", index, binding.parameterByteOffset,
                                                       static_cast<u8>(binding.type));
            if (binding.resourceAssetID == FileFormat::INVALID_ASSET_ID)
            {
                if ((binding.flags & Material::ResourceBindingFlags_Optional) == 0)
                    return Fail<MaterialInstanceAssetView>(DiagnosticCode::MissingRequiredReference, "resourceBindings.resourceAssetID", index);
                ++result.optionalMissingResourceReferences;
            }
        }

        for (u32 index = 0; index < result.view.animationBindings.size(); ++index)
        {
            const Material::MaterialAnimationBinding& binding = result.view.animationBindings[index];
            if (binding.materialAnimationAssetID == FileFormat::INVALID_ASSET_ID)
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::MissingRequiredReference, "animationBindings.materialAnimationAssetID", index);
            if (binding.timeSource > Material::AnimationTimeSource::AnimationController)
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::UnsupportedEnum, "animationBindings.timeSource", index,
                                                       static_cast<u8>(binding.timeSource),
                                                       static_cast<u8>(Material::AnimationTimeSource::AnimationController));
            if ((binding.flags & ~ANIMATION_FLAGS) != 0)
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::UnsupportedFlags, "animationBindings.flags", index, binding.flags, ANIMATION_FLAGS);
            if (binding.parameterByteOffset % alignof(u32) != 0 || binding.parameterByteOffset > root.parameterDataSize ||
                sizeof(u32) > root.parameterDataSize - binding.parameterByteOffset || !FindParameter(material.parameters, binding.parameterByteOffset))
                return Fail<MaterialInstanceAssetView>(DiagnosticCode::InvalidRange, "animationBindings.parameterByteOffset", index,
                                                       binding.parameterByteOffset, root.parameterDataSize);
        }

        return result;
    }
} // namespace MaterialLoading
