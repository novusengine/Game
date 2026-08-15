#include <MaterialCooker/AuthoredMaterialShaderGenerator.h>
#include <MaterialCooker/MaterialBackendShaderGenerator.h>
#include <MaterialCooker/MaterialCooker.h>
#include <MaterialCooker/MaterialSourceRegistry.h>

#include <catch2/catch2.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
using ParameterBlock =
    std::array<u8, FileFormat::Material::ABI::ParameterLayout::BLOCK_SIZE>;

struct TestMaterial {
  FileFormat::Material::MaterialAsset asset;
  ParameterBlock parameters = {};
  std::array<MaterialCooking::LegacyModelSourceUnit, 1> units;

  TestMaterial() {
    asset.programKey = 2;
    asset.programID = 1;
    asset.parameterBlockSize = static_cast<u32>(parameters.size());
    asset.parameterBlockAlignment = FileFormat::Material::ABI::PARAMETER_ALIGNMENT;
    units[0].textureCount = 1;
    units[0].semanticFlags =
        MaterialCooking::LegacyModelSourceUnitSemanticFlags_Complete;
  }

  MaterialCooking::AuthoredMaterialProgramView
  View(std::string_view key,
       FileFormat::Material::RasterClass rasterClass =
           FileFormat::Material::RasterClass::Opaque) const {
    return {key, &asset, {}, parameters, units, 0, rasterClass};
  }
};

MaterialCooking::MaterialProgramAssignment
Assignment(std::string_view key, std::string_view function,
           std::string_view coverage = "EvaluateCoverage_Test") {
  MaterialCooking::MaterialProgramAssignment result;
  result.canonicalKey = key;
  result.programFamily = "TestFamily";
  result.materialSource = "Generated/MaterialSources/Test.inc.slang";
  result.materialFunction = function;
  result.materialCoverageFunction = coverage;
  return result;
}

class CountingMeasurementAdapter final
    : public MaterialCooking::MaterialPipelineMeasurementAdapter {
public:
  MaterialCooking::MaterialPipelineMeasurementReport
  Measure(std::span<const MaterialCooking::CookedMaterialProgram>) override {
    ++calls;
    MaterialCooking::MaterialPipelineMeasurementReport report;
    report.status = MaterialCooking::MaterialMeasurementStatus::Available;
    return report;
  }

  u32 calls = 0;
};
} // namespace

TEST_CASE("Offline Material cook keeps advisory measurement optional",
          "[Rendering][MaterialCooker]") {
  TestMaterial material;
  const std::array sources = {material.View("program")};
  const std::array assignments = {
      Assignment("program", "EvaluateMaterial_Test")};

  MaterialCooking::MaterialCookRequest request;
  request.sourcePrograms = sources;
  request.assignments = assignments;
  request.measurementMode = MaterialCooking::MaterialMeasurementMode::Advisory;
  const MaterialCooking::MaterialCookResult result =
      MaterialCooking::MaterialCooker::Cook(request);

  CHECK(result.functionalCookSucceeded);
  CHECK(result);
  CHECK(result.measurement.report.status ==
        MaterialCooking::MaterialMeasurementStatus::Unavailable);
}

TEST_CASE("Offline Material cook does not measure invalid functional inputs",
          "[Rendering][MaterialCooker]") {
  TestMaterial material;
  material.asset.programKey =
      FileFormat::Material::INVALID_MATERIAL_PROGRAM_KEY;
  const std::array sources = {material.View("program")};
  const std::array assignments = {
      Assignment("program", "EvaluateMaterial_Test")};
  CountingMeasurementAdapter adapter;

  MaterialCooking::MaterialCookRequest request;
  request.sourcePrograms = sources;
  request.assignments = assignments;
  request.measurementMode = MaterialCooking::MaterialMeasurementMode::Required;
  request.measurementAdapter = &adapter;
  const MaterialCooking::MaterialCookResult result =
      MaterialCooking::MaterialCooker::Cook(request);

  CHECK_FALSE(result.functionalCookSucceeded);
  CHECK(adapter.calls == 0);
}

TEST_CASE("Authored Material source declarations carry source mapping and "
          "family policy",
          "[Rendering][MaterialCooker]") {
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     "NovusMaterialSourceRegistryTest";
  const std::filesystem::path authored = root / "Material" / "Authored";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(authored, error);
  REQUIRE_FALSE(error);

  const std::filesystem::path sourcePath = authored / "Test.mat.slang";
  {
    std::ofstream output(sourcePath, std::ios::binary);
    output << "material CompatibilityTest\n"
              "{\n"
              "    programFamily = CompatibilityCommon;\n"
              "    pixelShaderIDs = [0, 7];\n"
              "}\n"
              "SurfaceDescription EvaluateMaterial(MaterialEvaluationContext "
              "context)\n"
              "{\n"
              "    return MakeDefaultSurface(context.input);\n"
              "}\n"
              "float EvaluateCoverage(MaterialEvaluationContext context)\n"
              "{\n"
              "    return 1.0f;\n"
              "}\n";
  }

  const auto loaded =
      MaterialCooking::MaterialSourceRegistryIO::Load(authored, root);
  REQUIRE(loaded);
  REQUIRE(loaded.registry.materials.size() == 1);
  const auto &material = loaded.registry.materials[0];
  CHECK(material.name == "CompatibilityTest");
  CHECK(material.programFamily == "CompatibilityCommon");
  CHECK(material.pixelShaderIDs == std::vector<u8>{0, 7});
  CHECK(material.materialFunction == "EvaluateMaterial_CompatibilityTest");
  CHECK(material.materialCoverageFunction ==
        "EvaluateCoverage_CompatibilityTest");
  CHECK(material.generatedSource == "Generated/MaterialSources/Test.inc.slang");

  std::string generationError;
  REQUIRE(MaterialCooking::MaterialSourceRegistryIO::WriteGeneratedSources(
      loaded.registry, root, generationError));
  std::ifstream generatedInput(root / material.generatedSource,
                               std::ios::binary);
  const std::string generated{std::istreambuf_iterator<char>(generatedInput),
                              std::istreambuf_iterator<char>()};
  CHECK(generated.find(
            "#define EvaluateMaterial EvaluateMaterial_CompatibilityTest") !=
        std::string::npos);
  CHECK(generated.find(
            "#define EvaluateCoverage EvaluateCoverage_CompatibilityTest") !=
        std::string::npos);

  std::filesystem::remove_all(root, error);
}

TEST_CASE(
    "Generated Material groups keep surface, coverage, and lighting separate",
    "[Rendering][MaterialCooker]") {
  TestMaterial material;
  const std::array sources = {
      material.View("program", FileFormat::Material::RasterClass::AlphaTest)};
  const std::array assignments = {
      Assignment("program", "EvaluateMaterial_Test")};

  MaterialCooking::MaterialCookRequest request;
  request.sourcePrograms = sources;
  request.assignments = assignments;
  const MaterialCooking::MaterialCookResult result =
      MaterialCooking::MaterialCooker::Cook(request);
  REQUIRE(result);

  const u16 group = result.plan.programs[0].rasterRoutes[1].executionGroupID;
  const std::string source =
      MaterialCooking::AuthoredMaterialShaderGenerator::GenerateGroupSource(
          result.plan, group);
  CHECK(source.find("EvaluateMaterial_Test(context)") != std::string::npos);
  CHECK(source.find("BlendCompatibilityMaterialFirstLayer(surface, unit.w)") != std::string::npos);
  CHECK(source.find("EvaluateCoverage_Test(context)") != std::string::npos);
  CHECK(source.find("float coverage = EvaluateCoverageFunction") !=
        std::string::npos);
  CHECK(source.find("for (uint index = 1u; index < range.y; ++index)") !=
        std::string::npos);
  CHECK(source.find("BlendCompatibilityMaterialLayerCoverage") !=
        std::string::npos);
  CHECK(source.find("float coverage = 1.0f") == std::string::npos);
  CHECK(source.find("EvaluateLightingModel") == std::string::npos);

  const std::string coverage =
      MaterialCooking::MaterialBackendShaderGenerator::GenerateCoverageSource(
          result.plan);
  CHECK(coverage.find("EvaluateCookedMaterialCoverage") != std::string::npos);
  CHECK(coverage.find("EvaluateCookedMaterial(") == std::string::npos);

  const std::string forward =
      MaterialCooking::MaterialBackendShaderGenerator::GenerateForwardSource(
          result.plan);
  CHECK(forward.find("EvaluateCookedMaterial(") != std::string::npos);
  CHECK(forward.find("EvaluateLightingModel(") != std::string::npos);
}

TEST_CASE("Material routing is independent of manifest and declaration order",
          "[Rendering][MaterialCooker]") {
  std::array<TestMaterial, 2> materials;
  materials[0].asset.programKey = 11;
  materials[1].asset.programKey = 21;
  const std::array forwardSources = {materials[1].View("program-b"),
                                     materials[0].View("program-a")};
  const std::array reverseSources = {forwardSources[1], forwardSources[0]};
  const std::array forwardAssignments = {
      Assignment("program-a", "EvaluateMaterial_A"),
      Assignment("program-b", "EvaluateMaterial_B")};
  const std::array reverseAssignments = {forwardAssignments[1],
                                         forwardAssignments[0]};

  MaterialCooking::MaterialCookRequest firstRequest;
  firstRequest.sourcePrograms = forwardSources;
  firstRequest.assignments = reverseAssignments;
  const auto first = MaterialCooking::MaterialCooker::Cook(firstRequest);
  MaterialCooking::MaterialCookRequest secondRequest;
  secondRequest.sourcePrograms = reverseSources;
  secondRequest.assignments = forwardAssignments;
  const auto second = MaterialCooking::MaterialCooker::Cook(secondRequest);

  REQUIRE(first);
  REQUIRE(second);
  CHECK(first.plan.sourceManifestFingerprint ==
        second.plan.sourceManifestFingerprint);
  CHECK(first.plan.routingFingerprint == second.plan.routingFingerprint);
  CHECK(first.plan.functionalCookFingerprint ==
        second.plan.functionalCookFingerprint);
}
