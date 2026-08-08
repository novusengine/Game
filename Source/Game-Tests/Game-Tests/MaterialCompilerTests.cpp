#include <MaterialCooker/LegacyMaterialCompiler.h>

#include <catch2/catch2.hpp>

#include <array>

namespace
{
    FileFormat::Material::MaterialAsset MakeMaterial()
    {
        FileFormat::Material::MaterialAsset material;
        material.programKey = 4201;
        material.programID = 42;
        material.parameterBlockSize = FileFormat::Material::ABI::ParameterLayout::BLOCK_SIZE;
        material.parameterBlockAlignment = FileFormat::Material::ABI::PARAMETER_ALIGNMENT;
        return material;
    }

    MaterialCooking::LegacyModelSourceUnit MakeUnit(u16 shaderID, u8 textureCount,
                                                     u8 layer = 0, u8 flags = 0)
    {
        MaterialCooking::LegacyModelSourceUnit unit;
        unit.authoredShaderID = shaderID;
        unit.textureCount = textureCount;
        unit.layer = layer;
        unit.flags = flags;
        unit.semanticFlags = MaterialCooking::LegacyModelSourceUnitSemanticFlags_Complete;
        return unit;
    }
}

TEST_CASE("Material compiler resolves retained legacy shader signatures", "[Rendering][MaterialCompiler]")
{
    const std::array units = {MakeUnit(0, 1)};
    const auto result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, units, 0, FileFormat::Material::RasterClass::Opaque, 0);

    REQUIRE(result);
    CHECK(result.program.sourceProgramKey == 4201);
    CHECK(result.program.sourceProgramID == 42);
    CHECK(result.program.unitCount == 1);
    CHECK(result.program.textureCount == 1);
    CHECK(result.program.units[0].pixelShaderID == 0);
    CHECK(result.program.units[0].vertexShaderID == 0);
    CHECK(result.program.parameterBlockSize == FileFormat::Material::ABI::ParameterLayout::BLOCK_SIZE);
}

TEST_CASE("Material compiler expands layered units using flattened texture offsets",
          "[Rendering][MaterialCompiler]")
{
    std::array units = {MakeUnit(0, 1), MakeUnit(16, 1, 3, 8)};
    units[1].blendMode = 2;
    const auto result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, units, 0, FileFormat::Material::RasterClass::Opaque, 1);

    REQUIRE(result);
    CHECK(result.program.unitCount == 2);
    CHECK(result.program.textureCount == 2);
    CHECK(result.program.units[1].textureOffset == 1);
    CHECK(result.program.units[1].pixelShaderID == 1);
    CHECK(result.program.units[1].layer == 3);
    CHECK(result.program.units[1].flags == 8);
}

TEST_CASE("Material compiler resolves negative legacy shader-table IDs", "[Rendering][MaterialCompiler]")
{
    const std::array units = {MakeUnit(0x8001u, 2)};
    const auto result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, units, 0, FileFormat::Material::RasterClass::Opaque, 0);

    REQUIRE(result);
    CHECK(result.program.units[0].pixelShaderID == 13);
    CHECK(result.program.units[0].vertexShaderID == 3);
    CHECK(result.program.textureCount == 2);
}

TEST_CASE("Material compiler rejects incomplete and inconsistent contracts",
          "[Rendering][MaterialCompiler]")
{
    std::array units = {MakeUnit(0, 1), MakeUnit(16, 1)};
    units[1].semanticFlags = MaterialCooking::LegacyModelSourceUnitSemanticFlags_None;
    auto result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, units, 0, FileFormat::Material::RasterClass::Opaque, 1);
    CHECK(result.error == MaterialCooking::MaterialCompileError::MissingSourceBlendMetadata);

    units[1].semanticFlags = MaterialCooking::LegacyModelSourceUnitSemanticFlags_Complete;
    result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, units, 0, FileFormat::Material::RasterClass::Opaque, 0);
    CHECK(result.error == MaterialCooking::MaterialCompileError::ExecutionGroupMismatch);

    const std::array unsupported = {MakeUnit(0x8024u, 1)};
    result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, unsupported, 0, FileFormat::Material::RasterClass::Opaque, 0);
    CHECK(result.error == MaterialCooking::MaterialCompileError::UnsupportedShader);
}

TEST_CASE("Material compiler rejects an invalid canonical program key",
          "[Rendering][MaterialCompiler]")
{
    FileFormat::Material::MaterialAsset material = MakeMaterial();
    material.programKey = FileFormat::Material::INVALID_MATERIAL_PROGRAM_KEY;
    const std::array units = {MakeUnit(0, 1)};
    const auto result = MaterialCooking::LegacyMaterialCompiler::Compile(
        material, {}, units, 0, FileFormat::Material::RasterClass::Opaque, 0);
    CHECK(result.error == MaterialCooking::MaterialCompileError::InvalidProgramKey);
}

TEST_CASE("Offline Material compiler retains complete per-unit source semantics",
          "[Rendering][MaterialCompiler]")
{
    std::array units = {MakeUnit(0, 1), MakeUnit(16, 1, 2, 145)};
    units[1].blendMode = 4;
    units[1].sourceMaterialKind = 1;
    units[1].sourceMaterialFlags = 0x12345678u;
    units[1].sourceBlendMode = 4;
    units[1].semanticFlags |= MaterialCooking::LegacyModelSourceUnitSemanticFlags_Unfogged;

    const auto result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, units, 1, FileFormat::Material::RasterClass::Transparent, 5);
    REQUIRE(result);
    CHECK(result.program.units[1].textureOffset == 1);
    CHECK(result.program.units[1].flags == 145);
    CHECK(result.program.units[1].sourceMaterialFlags == 0x12345678u);
}

TEST_CASE("Offline Material compiler accepts valid untextured model programs",
          "[Rendering][MaterialCompiler]")
{
    const std::array units = {MakeUnit(4, 0)};
    const auto result = MaterialCooking::LegacyMaterialCompiler::Compile(
        MakeMaterial(), {}, units, 0, FileFormat::Material::RasterClass::Opaque, 0);
    REQUIRE(result);
    CHECK(result.program.textureCount == 0);
    CHECK(result.program.units[0].pixelShaderID == 0xFFu);
}
