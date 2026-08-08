#include <MaterialCooker/AuthoredMaterialShaderGenerator.h>
#include <MaterialCooker/MaterialCookPlan.h>
#include <MaterialCooker/MaterialPackWriter.h>
#include <Game-Lib/Rendering/Material/MaterialProgramLibrary.h>

#include <catch2/catch2.hpp>

#include <algorithm>
#include <array>
#include <filesystem>

namespace
{
    using ParameterBlock = std::array<u8, FileFormat::Material::ABI::ParameterLayout::BLOCK_SIZE>;

    struct TestSource
    {
        FileFormat::Material::MaterialAsset material;
        ParameterBlock parameters = {};
        std::array<MaterialCooking::LegacyModelSourceUnit, 1> units;

        explicit TestSource(u32 programID)
        {
            material.programKey = static_cast<u64>(programID) + 1;
            material.programID = programID;
            material.parameterBlockSize = static_cast<u32>(parameters.size());
            material.parameterBlockAlignment = FileFormat::Material::ABI::PARAMETER_ALIGNMENT;
            units[0].textureCount = 1;
            units[0].semanticFlags = MaterialCooking::LegacyModelSourceUnitSemanticFlags_Complete;
        }

        MaterialCooking::AuthoredMaterialProgramView View(
            std::string_view key, u16 lightingModelID = 0,
            FileFormat::Material::RasterClass rasterClass = FileFormat::Material::RasterClass::Opaque) const
        {
            return {key, &material, {}, parameters, units, lightingModelID, rasterClass};
        }
    };

    MaterialCooking::MaterialProgramAssignment Assignment(
        std::string_view key, std::string_view family, std::string_view source,
        std::string_view function, std::string_view coverage = {})
    {
        MaterialCooking::MaterialProgramAssignment result;
        result.canonicalKey = key;
        result.programFamily = family;
        result.materialSource = source;
        result.materialFunction = function;
        result.materialCoverageFunction = coverage;
        return result;
    }
}

TEST_CASE("Material cook plan derives deterministic family and behavior routing",
          "[Rendering][MaterialCooker]")
{
    std::array sourceData = {TestSource(30), TestSource(10), TestSource(20)};
    const std::array sources = {
        sourceData[0].View("program-c", 0, FileFormat::Material::RasterClass::AlphaTest),
        sourceData[1].View("program-a"), sourceData[2].View("program-b")};
    const std::array assignments = {
        Assignment("program-a", "Common", "Generated/A.inc.slang", "EvaluateMaterial_A"),
        Assignment("program-b", "Common", "Generated/B.inc.slang", "EvaluateMaterial_B"),
        Assignment("program-c", "Common", "Generated/C.inc.slang", "EvaluateMaterial_C",
                   "EvaluateCoverage_C")};

    const auto result = MaterialCooking::MaterialCookPlanBuilder::Build(sources, assignments);
    REQUIRE(result);
    REQUIRE(result.programs.size() == 3);
    CHECK(result.programs[0].canonicalKey == "program-a");
    CHECK(result.programs[1].canonicalKey == "program-b");
    CHECK(result.programs[2].canonicalKey == "program-c");
    CHECK(result.programs[0].rasterRoutes[0].executionGroupID == 0);
    CHECK(result.programs[2].rasterRoutes[1].executionGroupID == 2);
}

TEST_CASE("Material cook plan shares identical behavior and separates distinct behavior",
          "[Rendering][MaterialCooker]")
{
    std::array sourceData = {TestSource(10), TestSource(20), TestSource(30)};
    const std::array sources = {
        sourceData[0].View("program-a"), sourceData[1].View("program-b"),
        sourceData[2].View("program-c")};
    const std::array assignments = {
        Assignment("program-a", "Common", "Generated/A.inc.slang", "EvaluateMaterial_A"),
        Assignment("program-b", "Common", "Generated/A.inc.slang", "EvaluateMaterial_A"),
        Assignment("program-c", "Common", "Generated/C.inc.slang", "EvaluateMaterial_C")};

    const auto result = MaterialCooking::MaterialCookPlanBuilder::Build(sources, assignments);
    REQUIRE(result);
    CHECK(result.programs[0].rasterRoutes[0].groupLocalProgramID == 0);
    CHECK(result.programs[1].rasterRoutes[0].groupLocalProgramID == 0);
    CHECK(result.programs[2].rasterRoutes[0].groupLocalProgramID == 1);
}

TEST_CASE("Material cook plan routes each program family to its own six group classes",
          "[Rendering][MaterialCooker]")
{
    std::array sourceData = {TestSource(10), TestSource(20)};
    const std::array sources = {sourceData[0].View("program-a"), sourceData[1].View("program-b")};
    const std::array assignments = {
        Assignment("program-a", "FamilyA", "Generated/A.inc.slang", "EvaluateMaterial_A"),
        Assignment("program-b", "FamilyB", "Generated/B.inc.slang", "EvaluateMaterial_B")};

    const auto result = MaterialCooking::MaterialCookPlanBuilder::Build(sources, assignments);
    REQUIRE(result);
    CHECK(result.programs[0].rasterRoutes[0].executionGroupID == 0);
    CHECK(result.programs[0].rasterRoutes[1].executionGroupID == 2);
    CHECK(result.programs[0].rasterRoutes[2].executionGroupID == 4);
    CHECK(result.programs[1].rasterRoutes[0].executionGroupID == 6);
    CHECK(result.programs[1].rasterRoutes[1].executionGroupID == 8);
    CHECK(result.programs[1].rasterRoutes[2].executionGroupID == 10);
}

TEST_CASE("Alpha-tested Material without coverage keeps every sample",
          "[Rendering][MaterialCooker]")
{
    TestSource source(10);
    const std::array sources = {
        source.View("program", 0, FileFormat::Material::RasterClass::AlphaTest)};
    const std::array assignments = {
        Assignment("program", "Common", "Generated/Test.inc.slang", "EvaluateMaterial_Test")};
    const auto result = MaterialCooking::MaterialCookPlanBuilder::Build(sources, assignments);
    REQUIRE(result);
    const std::string shader =
        MaterialCooking::AuthoredMaterialShaderGenerator::GenerateGroupSource(
            result, result.programs[0].rasterRoutes[1].executionGroupID);
    CHECK(shader.find("default: return 1.0f") != std::string::npos);
}

TEST_CASE("Material cook plan rejects unsupported LightingModels and duplicate program keys",
          "[Rendering][MaterialCooker]")
{
    std::array sourceData = {TestSource(10), TestSource(20)};
    sourceData[1].material.programKey = sourceData[0].material.programKey;
    const std::array sources = {
        sourceData[0].View("program-a", static_cast<u16>(FileFormat::Material::ABI::LightingModel::Count)),
        sourceData[1].View("program-b")};
    const std::array assignments = {
        Assignment("program-a", "Common", "Generated/A.inc.slang", "EvaluateMaterial_A"),
        Assignment("program-b", "Common", "Generated/B.inc.slang", "EvaluateMaterial_B")};

    const auto result = MaterialCooking::MaterialCookPlanBuilder::Build(sources, assignments);
    REQUIRE_FALSE(result);
    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.error == MaterialCooking::MaterialCookPlanError::UnsupportedLightingModel;
    }));
    CHECK(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.error == MaterialCooking::MaterialCookPlanError::DuplicateSourceProgramKey;
    }));
}

TEST_CASE("MaterialPack preserves derived runtime routing", "[Rendering][MaterialCooker]")
{
    TestSource source(42);
    source.material.flags = FileFormat::Material::MaterialFlags_HasCoverageFunction;
    const std::array sources = {source.View("program")};
    const std::array assignments = {
        Assignment("program", "Common", "Generated/Test.inc.slang", "EvaluateMaterial_Test",
                   "EvaluateCoverage_Test")};
    const auto plan = MaterialCooking::MaterialCookPlanBuilder::Build(sources, assignments);
    REQUIRE(plan);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "NovusMaterialPackTest.matpack";
    std::error_code filesystemError;
    std::filesystem::remove(path, filesystemError);
    std::string error;
    REQUIRE(MaterialCooking::MaterialPackWriter::Save(path, plan, error));

    MaterialLoading::MaterialProgramLibrary library;
    REQUIRE(library.Load(path, error));
    const MaterialLoading::MaterialAssetView view{
        .root = source.material, .parameters = {}, .defaultParameterData = source.parameters};
    const auto* program = library.Resolve(view);
    REQUIRE(program != nullptr);
    CHECK(program->programKey == source.material.programKey);
    CHECK(program->rasterRoutes[0].executionGroupID == 0);
    CHECK(program->rasterRoutes[1].executionGroupID == 2);
    CHECK(program->rasterRoutes[2].executionGroupID == 4);

    std::filesystem::remove(path, filesystemError);
}
