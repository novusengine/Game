#pragma once
#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
#include "Game-Lib/Rendering/Asset/AssetValidation.h"

#include <FileFormat/Novus/Model/Material.h>

#include <span>

namespace MaterialLoading
{
    struct MaterialAssetView
    {
        FileFormat::Material::MaterialAsset root;
        std::span<const FileFormat::Material::ParameterDefinition> parameters;
        std::span<const u8> defaultParameterData;
    };

    struct MaterialInstanceAssetView
    {
        FileFormat::Material::MaterialInstanceAsset root;
        std::span<const u8> parameterData;
        std::span<const FileFormat::Material::TextureBinding> textureBindings;
        std::span<const FileFormat::Material::MaterialAnimationBinding> animationBindings;
    };

    template <typename T>
    struct MaterialAssetReadResult
    {
        T view;
        AssetLoading::Diagnostic diagnostic;
        u32 optionalMissingResourceReferences = 0;

        explicit operator bool() const
        {
            return !diagnostic;
        }
    };

    // Decodes CPU-side material and material-instance payloads into borrowed CPU-side asset views.
    // Borrowed views expose offline-prepared material data without copying its parameter payloads.
    class MaterialAssetReader
    {
      public:
        // Returned spans borrow payload and remain valid only while it remains alive.
        static MaterialAssetReadResult<MaterialAssetView> ReadMaterial(std::span<const u8> payload, AssetLoading::ValidationMode validationMode = AssetLoading::ValidationMode::Default);
        // Dependency-free structural decode used to discover the referenced Material before optional cross-asset validation.
        static MaterialAssetReadResult<MaterialInstanceAssetView> DecodeMaterialInstance(std::span<const u8> payload);
        static MaterialAssetReadResult<MaterialInstanceAssetView> ReadMaterialInstance(std::span<const u8> payload, const MaterialAssetView& material,
                                                                                       AssetLoading::ValidationMode validationMode = AssetLoading::ValidationMode::Default);
    };
} // namespace MaterialLoading
