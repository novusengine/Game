#pragma once

#include "MaterialCooker.h"

#include <FileFormat/Novus/Model/MaterialABI.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace MaterialCooking
{
    struct MaterialCookInputProgram
    {
        std::string canonicalKey;
        FileFormat::Material::MaterialAsset material;
        std::vector<FileFormat::Material::ParameterDefinition> parameters;
        std::array<u8, FileFormat::Material::ABI::ParameterLayout::BLOCK_SIZE> parameterData = {};
        std::vector<LegacyModelSourceUnit> sourceUnits;
        u16 lightingModelID = 0;
        FileFormat::Material::RasterClass rasterClass = FileFormat::Material::RasterClass::Opaque;
        u8 unitCount = 0;
    };

    struct MaterialCookManifest
    {
        std::vector<MaterialCookInputProgram> programs;
    };

    struct MaterialCookManifestLoadResult
    {
        MaterialCookManifest manifest;
        std::string error;

        explicit operator bool() const { return error.empty(); }
    };

    // Loads the CPU-side deduplicated source-program manifest consumed by the standalone cooker.
    // Manifest metadata identifies source behavior while reviewed assignments select authored materials.
    class MaterialProgramManifestIO
    {
      public:
        static MaterialCookManifestLoadResult Load(const std::filesystem::path& path);
        static std::vector<AuthoredMaterialProgramView> MakeViews(
            const MaterialCookManifest& manifest);
        static bool SaveCookReport(const std::filesystem::path& path,
                                   const MaterialCookResult& result);
    };
}
