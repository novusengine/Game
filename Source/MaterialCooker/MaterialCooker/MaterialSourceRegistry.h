#pragma once

#include "MaterialCookPlan.h"

#include <filesystem>
#include <string>
#include <vector>

namespace MaterialCooking
{
    struct MaterialCookManifest;

    struct AuthoredMaterialInstance
    {
        std::string name;
        std::string path;
        u32 flags = FileFormat::Material::MaterialInstanceFlags_None;
        u16 lightingModelID = static_cast<u16>(FileFormat::Material::ABI::LightingModel::Standard);
    };

    struct AuthoredMaterialSource
    {
        std::string name;
        std::string programFamily;
        std::string assetPath;
        std::string materialSource;
        std::string generatedSource;
        std::string materialFunction;
        std::string materialCoverageFunction;
        std::string sourceBody;
        std::vector<u8> pixelShaderIDs;
        std::vector<AuthoredMaterialInstance> instances;
        FileFormat::Material::RasterClass rasterClass = FileFormat::Material::RasterClass::Opaque;

        bool IsNative() const { return !assetPath.empty(); }
    };

    struct MaterialSourceRegistry
    {
        std::vector<AuthoredMaterialSource> materials;
    };

    struct MaterialSourceRegistryLoadResult
    {
        MaterialSourceRegistry registry;
        std::string error;

        explicit operator bool() const { return error.empty(); }
    };

    struct MaterialSourceResolveResult
    {
        std::vector<MaterialProgramAssignment> programs;
        std::string error;

        explicit operator bool() const { return error.empty(); }
    };

    // Discovers CPU-side authored Material declarations and resolves source programs to them.
    // Co-located declarations keep shader behavior, source mappings, and program-family choices together.
    class MaterialSourceRegistryIO
    {
      public:
        static MaterialSourceRegistryLoadResult Load(
            const std::filesystem::path& authoredDirectory,
            const std::filesystem::path& shaderSourceDirectory);
        static MaterialSourceResolveResult Resolve(
            const MaterialSourceRegistry& registry,
            std::span<const AuthoredMaterialProgramView> sourcePrograms);
        static bool AppendNativePrograms(const MaterialSourceRegistry& registry, MaterialCookManifest& manifest, std::string& error);
        static bool WriteGeneratedSources(
            const MaterialSourceRegistry& registry,
            const std::filesystem::path& shaderSourceDirectory,
            std::string& error);
    };
}
