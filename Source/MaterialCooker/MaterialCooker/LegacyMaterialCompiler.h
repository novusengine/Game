#pragma once

#include <FileFormat/Novus/Model/Material.h>
#include <FileFormat/Novus/Model/MaterialABI.h>

#include <array>
#include <span>

namespace MaterialCooking
{
    enum class MaterialCompileError : u8
    {
        None,
        InvalidProgramKey,
        ParameterBlockTooSmall,
        MissingUnits,
        InvalidUnitTextureCount,
        TextureCountOverflow,
        UnsupportedShader,
        InvalidExecutionGroup,
        ExecutionGroupMismatch,
        InvalidParameterLayout,
        MissingSourceBlendMetadata,
        InstanceRestampUnavailable
    };

    // Complete per-unit semantics emitted by AssetConverter for offline Material cooking.
    // Raw source values remain available so compatibility Materials can reproduce legacy behavior.
    struct LegacyModelSourceUnit
    {
        u16 authoredShaderID = 0;
        u8 textureCount = 0;
        u8 layer = 0;
        u8 flags = 0;
        u8 blendMode = 0;
        u8 sourceMaterialKind = 0;
        u8 semanticFlags = 0;
        u32 sourceMaterialFlags = 0;
        u32 sourceBlendMode = 0;
    };

    enum LegacyModelSourceUnitSemanticFlags : u8
    {
        LegacyModelSourceUnitSemanticFlags_None = 0,
        LegacyModelSourceUnitSemanticFlags_Complete = 1u << 0,
        LegacyModelSourceUnitSemanticFlags_Unlit = 1u << 1,
        LegacyModelSourceUnitSemanticFlags_Unfogged = 1u << 2,
        LegacyModelSourceUnitSemanticFlags_TwoSided = 1u << 3
    };

    struct CompiledMaterialUnit
    {
        u16 authoredShaderID = 0;
        u8 pixelShaderID = 0;
        u8 vertexShaderID = 0;
        u8 textureOffset = 0;
        u8 textureCount = 0;
        u8 layer = 0;
        u8 flags = 0;
        u8 blendMode = 0;
        u8 sourceMaterialKind = 0;
        u8 semanticFlags = 0;
        u32 sourceMaterialFlags = 0;
        u32 sourceBlendMode = 0;
    };

    struct CompiledMaterialProgram
    {
        FileFormat::Material::MaterialProgramKey sourceProgramKey =
            FileFormat::Material::INVALID_MATERIAL_PROGRAM_KEY;
        u32 sourceProgramID = 0;
        u16 lightingModelID = 0;
        u16 executionGroupID = 0;
        u8 rasterClass = 0;
        u8 unitCount = 0;
        u8 textureCount = 0;
        u32 flags = 0;
        std::array<CompiledMaterialUnit, FileFormat::Material::ABI::LegacyModel::MAX_UNITS> units;
        u64 parameterLayoutHash = 0;
        u32 parameterBlockSize = 0;
        u32 parameterBlockAlignment = 0;
    };

    struct MaterialCompileResult
    {
        CompiledMaterialProgram program;
        MaterialCompileError error = MaterialCompileError::None;
        u32 errorUnit = 0;
        u32 observed = 0;

        explicit operator bool() const { return error == MaterialCompileError::None; }
    };

    // Compiles CPU-side normalized legacy metadata into bounded offline Material program records.
    // Early semantic rejection keeps incomplete authored inputs out of generated group shaders.
    class LegacyMaterialCompiler
    {
      public:
        static MaterialCompileResult Compile(
            const FileFormat::Material::MaterialAsset& material,
            std::span<const FileFormat::Material::ParameterDefinition> parameters,
            std::span<const LegacyModelSourceUnit> sourceUnits,
            u16 lightingModelID, FileFormat::Material::RasterClass rasterClass,
            u16 executionGroupID,
            bool requireCompleteSourceSemantics = true);
        static bool ResolveShaderIDs(u16 authoredShaderID, u8 textureCount,
                                     u8& outPixelShaderID, u8& outVertexShaderID);
        static const char* Describe(MaterialCompileError error);
    };
}
