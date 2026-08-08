#include "LegacyMaterialCompiler.h"

namespace
{
    using namespace MaterialCooking;

    struct ShaderPair
    {
        u8 pixel;
        u8 vertex;
    };

    constexpr std::array<ShaderPair, 36> SHADER_TABLE = {{
        {12, 3}, {13, 3}, {14, 3}, {15, 6}, {16, 3}, {13, 7}, {16, 7}, {17, 3}, {18, 3},
        {19, 6}, {20, 7}, {21, 3}, {22, 3}, {23, 3}, {23, 7}, {20, 2}, {24, 3}, {25, 6},
        {26, 0}, {33, 9}, {27, 11}, {6, 12}, {28, 2}, {29, 7}, {25, 11}, {33, 13}, {30, 14},
        {31, 2}, {32, 14}, {34, 7}, {35, 15}, {35, 16}, {0, 0}, {7, 12}, {1, 9}, {36, 12}
    }};

    bool ResolveShader(u16 authoredShaderID, u8 textureCount, u8& outPixel, u8& outVertex)
    {
        if (textureCount == 0)
        {
            outPixel = 0xFFu;
            outVertex = 0;
            return true;
        }
        const i16 signedShaderID = static_cast<i16>(authoredShaderID);
        if (signedShaderID < 0)
        {
            const u32 tableIndex = authoredShaderID & 0x7FFFu;
            if (tableIndex >= SHADER_TABLE.size())
                return false;
            outPixel = SHADER_TABLE[tableIndex].pixel;
            outVertex = SHADER_TABLE[tableIndex].vertex;
            return outPixel <= 36;
        }

        if (textureCount == 1)
        {
            outPixel = (authoredShaderID & 0x70u) != 0u ? 1u : 0u;
            if ((authoredShaderID & 0x80u) != 0u)
                outVertex = 1;
            else
                outVertex = (authoredShaderID & 0x4000u) != 0u ? 10u : 0u;
            return true;
        }

        if ((authoredShaderID & 0x80u) != 0u)
            outVertex = static_cast<u8>(((authoredShaderID & 0x8u) >> 3u) | 4u);
        else
            outVertex = (authoredShaderID & 0x8u) != 0u ? 3u :
                static_cast<u8>(5u * ((authoredShaderID & 0x4000u) == 0u) + 2u);

        if ((authoredShaderID & 0x70u) != 0u)
        {
            switch (authoredShaderID & 0x7u)
            {
            case 3: outPixel = 8; break;
            case 4: outPixel = 7; break;
            case 6: outPixel = 9; break;
            case 7: outPixel = 10; break;
            default: outPixel = 6; break;
            }
        }
        else
        {
            switch (authoredShaderID & 0x7u)
            {
            case 0: outPixel = 5; break;
            case 3:
            case 7: outPixel = 13; break;
            case 4: outPixel = 3; break;
            case 6: outPixel = 4; break;
            default: outPixel = 2; break;
            }
        }
        return true;
    }

    u16 ExpectedExecutionGroup(FileFormat::Material::RasterClass rasterClass, u32 unitCount)
    {
        const u16 layeredOffset = unitCount > 1 ? 1u : 0u;
        switch (rasterClass)
        {
        case FileFormat::Material::RasterClass::Opaque:
            return static_cast<u16>(FileFormat::Material::ABI::ExecutionGroup::OpaqueSimple) + layeredOffset;
        case FileFormat::Material::RasterClass::AlphaTest:
            return static_cast<u16>(FileFormat::Material::ABI::ExecutionGroup::AlphaTestSimple) + layeredOffset;
        case FileFormat::Material::RasterClass::Transparent:
            return static_cast<u16>(FileFormat::Material::ABI::ExecutionGroup::TransparentSimple) + layeredOffset;
        default: return 0xFFFFu;
        }
    }

    MaterialCompileResult Fail(MaterialCompileError error, u32 unit = 0, u32 observed = 0)
    {
        MaterialCompileResult result;
        result.error = error;
        result.errorUnit = unit;
        result.observed = observed;
        return result;
    }
}

namespace MaterialCooking
{
    bool LegacyMaterialCompiler::ResolveShaderIDs(
        u16 authoredShaderID, u8 textureCount,
        u8& outPixelShaderID, u8& outVertexShaderID)
    {
        return ResolveShader(authoredShaderID, textureCount,
                             outPixelShaderID, outVertexShaderID);
    }

    MaterialCompileResult LegacyMaterialCompiler::Compile(
        const FileFormat::Material::MaterialAsset& material,
        std::span<const FileFormat::Material::ParameterDefinition> parameters,
        std::span<const LegacyModelSourceUnit> sourceUnits,
        u16 lightingModelID, FileFormat::Material::RasterClass rasterClass,
        u16 executionGroupID,
        bool requireCompleteSourceSemantics)
    {
        if (material.programKey == FileFormat::Material::INVALID_MATERIAL_PROGRAM_KEY)
            return Fail(MaterialCompileError::InvalidProgramKey);
        if (material.parameterBlockSize != FileFormat::Material::ABI::ParameterLayout::BLOCK_SIZE ||
            material.parameterBlockAlignment < FileFormat::Material::ABI::PARAMETER_ALIGNMENT ||
            (material.parameterBlockAlignment & (material.parameterBlockAlignment - 1u)) != 0u)
            return Fail(MaterialCompileError::InvalidParameterLayout, 0, material.parameterBlockSize);
        if (executionGroupID >= FileFormat::Material::ABI::EXECUTION_GROUP_COUNT)
            return Fail(MaterialCompileError::InvalidExecutionGroup, 0, executionGroupID);
        if (sourceUnits.empty())
            return Fail(MaterialCompileError::MissingUnits);
        if (sourceUnits.size() > FileFormat::Material::ABI::LegacyModel::MAX_UNITS)
            return Fail(MaterialCompileError::TextureCountOverflow, 0,
                        static_cast<u32>(sourceUnits.size()));

        MaterialCompileResult result;
        result.program.sourceProgramKey = material.programKey;
        result.program.sourceProgramID = material.programID;
        result.program.lightingModelID = lightingModelID;
        result.program.executionGroupID = executionGroupID;
        result.program.rasterClass = static_cast<u8>(rasterClass);
        result.program.flags = material.flags;
        result.program.parameterLayoutHash =
            FileFormat::Material::CalculateParameterLayoutHash(parameters, material.parameterBlockSize);
        result.program.parameterBlockSize = material.parameterBlockSize;
        result.program.parameterBlockAlignment = material.parameterBlockAlignment;

        u32 textureOffset = 0;
        for (u32 unitIndex = 0; unitIndex < sourceUnits.size(); ++unitIndex)
        {
            const LegacyModelSourceUnit& source = sourceUnits[unitIndex];
            if (textureOffset + source.textureCount >
                FileFormat::Material::ABI::LegacyModel::MAX_TEXTURES)
                return Fail(MaterialCompileError::TextureCountOverflow, unitIndex,
                            textureOffset + source.textureCount);
            if (requireCompleteSourceSemantics &&
                (sourceUnits.size() > 1 ||
                 rasterClass == FileFormat::Material::RasterClass::Transparent) &&
                (source.semanticFlags & LegacyModelSourceUnitSemanticFlags_Complete) == 0)
                return Fail(MaterialCompileError::MissingSourceBlendMetadata, unitIndex,
                            static_cast<u32>(sourceUnits.size()));

            CompiledMaterialUnit& unit = result.program.units[result.program.unitCount];
            unit.authoredShaderID = source.authoredShaderID;
            unit.textureOffset = static_cast<u8>(textureOffset);
            unit.textureCount = source.textureCount;
            unit.layer = source.layer;
            unit.flags = source.flags;
            unit.blendMode = source.blendMode;
            unit.sourceMaterialKind = source.sourceMaterialKind;
            unit.semanticFlags = source.semanticFlags;
            unit.sourceMaterialFlags = source.sourceMaterialFlags;
            unit.sourceBlendMode = source.sourceBlendMode;
            if (!ResolveShader(unit.authoredShaderID, unit.textureCount,
                               unit.pixelShaderID, unit.vertexShaderID))
                return Fail(MaterialCompileError::UnsupportedShader, unitIndex,
                            unit.authoredShaderID);

            textureOffset += unit.textureCount;
            result.program.unitCount++;
        }

        result.program.textureCount = static_cast<u8>(textureOffset);

        const u16 expectedGroup = ExpectedExecutionGroup(rasterClass, result.program.unitCount);
        if (executionGroupID != expectedGroup)
            return Fail(MaterialCompileError::ExecutionGroupMismatch, 0,
                        (static_cast<u32>(expectedGroup) << 16u) | executionGroupID);

        return result;
    }

    const char* LegacyMaterialCompiler::Describe(MaterialCompileError error)
    {
        switch (error)
        {
        case MaterialCompileError::None: return "none";
        case MaterialCompileError::InvalidProgramKey: return "invalid_program_key";
        case MaterialCompileError::ParameterBlockTooSmall: return "parameter_block_too_small";
        case MaterialCompileError::MissingUnits: return "missing_units";
        case MaterialCompileError::InvalidUnitTextureCount: return "invalid_unit_texture_count";
        case MaterialCompileError::TextureCountOverflow: return "texture_count_overflow";
        case MaterialCompileError::UnsupportedShader: return "unsupported_shader";
        case MaterialCompileError::InvalidExecutionGroup: return "invalid_execution_group";
        case MaterialCompileError::ExecutionGroupMismatch: return "execution_group_mismatch";
        case MaterialCompileError::InvalidParameterLayout: return "invalid_parameter_layout";
        case MaterialCompileError::MissingSourceBlendMetadata: return "missing_source_blend_metadata";
        case MaterialCompileError::InstanceRestampUnavailable: return "instance_restamp_unavailable";
        }
        return "unknown";
    }
}
