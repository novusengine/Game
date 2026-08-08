#pragma once

#include "LegacyMaterialCompiler.h"

#include <FileFormat/Novus/Model/MaterialABI.h>
#include <FileFormat/Novus/Model/MaterialPack.h>

#include <span>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace MaterialCooking
{
    struct AuthoredMaterialProgramView
    {
        std::string_view canonicalKey;
        const FileFormat::Material::MaterialAsset* material = nullptr;
        std::span<const FileFormat::Material::ParameterDefinition> parameters;
        std::span<const u8> parameterData;
        std::span<const LegacyModelSourceUnit> sourceUnits;
        u16 lightingModelID = 0;
        FileFormat::Material::RasterClass rasterClass = FileFormat::Material::RasterClass::Opaque;
    };

    struct MaterialProgramAssignment
    {
        std::string_view canonicalKey;
        std::string_view programFamily;
        std::string_view materialSource;
        std::string_view materialFunction;
        std::string_view materialCoverageFunction;
        std::array<std::string_view, FileFormat::Material::ABI::LegacyModel::MAX_UNITS> unitMaterialSources;
        std::array<std::string_view, FileFormat::Material::ABI::LegacyModel::MAX_UNITS> unitMaterialFunctions;
        std::array<std::string_view, FileFormat::Material::ABI::LegacyModel::MAX_UNITS> unitCoverageFunctions;
        u8 authoredUnitCount = 0;
    };

    struct CookedMaterialProgram
    {
        std::string canonicalKey;
        std::string programFamily;
        std::string materialSource;
        std::string materialFunction;
        std::string materialCoverageFunction;
        CompiledMaterialProgram program;
        std::vector<FileFormat::Material::ParameterDefinition> parameters;
        std::array<FileFormat::Material::MaterialProgramRoute, 3> rasterRoutes;
        u16 groupLocalProgramID = 0;
        std::array<std::string, FileFormat::Material::ABI::LegacyModel::MAX_UNITS> unitMaterialSources;
        std::array<std::string, FileFormat::Material::ABI::LegacyModel::MAX_UNITS> unitMaterialFunctions;
        std::array<std::string, FileFormat::Material::ABI::LegacyModel::MAX_UNITS> unitCoverageFunctions;
        u8 authoredUnitCount = 0;
    };

    enum class MaterialCookPlanError : u8
    {
        None,
        EmptyCanonicalKey,
        DuplicateSourceProgram,
        DuplicateSourceProgramKey,
        DuplicateAssignment,
        MissingAssignment,
        MissingMaterialSource,
        MissingProgramFamily,
        InvalidMaterialSourceExtension,
        MissingMaterialFunction,
        MissingMaterialCoverageFunction,
        UnsupportedLightingModel,
        InvalidExecutionGroup,
        InvalidSourceProgram
    };

    struct MaterialCookPlanDiagnostic
    {
        MaterialCookPlanError error = MaterialCookPlanError::None;
        MaterialCompileError compileError = MaterialCompileError::None;
        std::string canonicalKey;
        u32 observed = 0;
    };

    struct MaterialCookPlan
    {
        std::vector<CookedMaterialProgram> programs;
        std::vector<MaterialCookPlanDiagnostic> diagnostics;
        u64 sourceManifestFingerprint = 0;
        u64 routingFingerprint = 0;
        u64 functionalCookFingerprint = 0;

        explicit operator bool() const { return diagnostics.empty(); }
    };

    // Builds the deterministic CPU-side program order consumed by offline shader generation.
    // Authored program families control grouping while behavior sorting keeps routing independent of load order.
    class MaterialCookPlanBuilder
    {
      public:
        static MaterialCookPlan Build(std::span<const AuthoredMaterialProgramView> sourcePrograms,
                                      std::span<const MaterialProgramAssignment> assignments,
                                      bool requireCompleteSourceSemantics = true);
        static const char* Describe(MaterialCookPlanError error);
    };
}
