#include "AuthoredMaterialShaderGenerator.h"
#include "MaterialBackendShaderGenerator.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace MaterialCooking {
namespace {
const FileFormat::Material::MaterialProgramRoute *
RouteForGroup(const CookedMaterialProgram &program, u16 executionGroupID) {
  const u32 rasterIndex =
      static_cast<u32>(
          FileFormat::Material::ABI::GetExecutionGroupClass(executionGroupID)) /
      2u;
  if (rasterIndex >= program.rasterRoutes.size())
    return nullptr;
  const FileFormat::Material::MaterialProgramRoute &route =
      program.rasterRoutes[rasterIndex];
  return route.executionGroupID == executionGroupID ? &route : nullptr;
}

bool WriteIfChanged(const std::filesystem::path &path,
                    std::string_view contents) {
  std::ifstream input(path, std::ios::binary);
  if (input) {
    const std::string existing{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    if (existing == contents)
      return true;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  return output.good();
}

bool IsAlphaTestGroup(u16 group) {
  const auto groupClass = FileFormat::Material::ABI::GetExecutionGroupClass(group);
  return groupClass == FileFormat::Material::ABI::ExecutionGroup::AlphaTestSimple || groupClass == FileFormat::Material::ABI::ExecutionGroup::AlphaTestLayered;
}

std::string GenerateActiveGroupSource(const std::set<u16> &groups) {
  std::ostringstream source;
  source << "// Generated active Material execution-group interface. Do not "
            "hand edit.\n";
  bool first = true;
  for (u16 group : groups) {
    source << (first ? "#if" : "#elif") << " MATERIAL_COOK_GROUP == " << group
           << "\n#include \"Generated/MaterialGroup" << group
           << ".inc.slang\"\n";
    first = false;
  }
  source << "#else\n#error Unsupported Material execution group.\n#endif\n\n"
            "SurfaceDescription EvaluateCookedMaterial(\n"
            "    uint groupLocalProgramID, MaterialEvaluationContext context)\n"
            "{\n";
  first = true;
  for (u16 group : groups) {
    source << (first ? "#if" : "#elif") << " MATERIAL_COOK_GROUP == " << group
           << "\n    return EvaluateCookedMaterialGroup" << group
           << "(groupLocalProgramID, context);\n";
    first = false;
  }
  source << "#endif\n}\n\n";
  first = true;
  for (u16 group : groups) {
    if (!IsAlphaTestGroup(group))
      continue;
    source
        << (first ? "#if" : "#elif") << " MATERIAL_COOK_GROUP == " << group
        << "\nfloat EvaluateCookedMaterialCoverage(\n"
           "    uint groupLocalProgramID, MaterialEvaluationContext context)\n"
           "{\n    return EvaluateCookedMaterialCoverageGroup"
        << group << "(groupLocalProgramID, context);\n}\n";
    first = false;
  }
  if (!first)
    source << "#endif\n\n";
  return source.str();
}

std::string GenerateAlphaTestGroupsSource(const std::set<u16> &groups) {
  std::ostringstream source;
  source << "// Generated alpha-test Material execution-group interface. Do "
            "not hand edit.\n";
  for (u16 group : groups) {
    if (IsAlphaTestGroup(group))
      source << "#include \"Generated/MaterialGroup" << group
             << ".inc.slang\"\n";
  }
  source << "\nfloat EvaluateAlphaTestMaterialCoverage(\n"
            "    uint executionGroup, uint groupLocalProgramID,\n"
            "    MaterialEvaluationContext context)\n"
            "{\n";
  for (u16 group : groups) {
    if (IsAlphaTestGroup(group))
      source << "    if (executionGroup == " << group
             << "u)\n"
                "        return EvaluateCookedMaterialCoverageGroup"
             << group << "(groupLocalProgramID, context);\n";
  }
  source << "    return 1.0f;\n}\n";
  return source.str();
}
} // namespace

std::string AuthoredMaterialShaderGenerator::GenerateGroupSource(
    const MaterialCookPlan &plan, u16 executionGroupID) {
  std::ostringstream source;
  source << "// Generated Material execution-group shader. Do not hand edit.\n"
            "#include \"Material/MaterialAuthoring.inc.slang\"\n"
            "#if MATERIAL_ABI_VERSION != "
         << FileFormat::Material::ABI::VERSION
         << "u\n"
            "#error Generated Material shader ABI does not match its authored "
            "sources.\n"
            "#endif\n";

  std::set<std::string_view> includedSources;
  for (const CookedMaterialProgram &cooked : plan.programs) {
    if (!RouteForGroup(cooked, executionGroupID))
      continue;
    if (cooked.authoredUnitCount == 0 &&
        includedSources.emplace(cooked.materialSource).second) {
      source << "#include \"" << cooked.materialSource << "\"\n";
    }
    for (u32 unit = 0; unit < cooked.authoredUnitCount; ++unit) {
      if (includedSources.emplace(cooked.unitMaterialSources[unit]).second)
        source << "#include \"" << cooked.unitMaterialSources[unit] << "\"\n";
    }
  }

  const std::string suffix = std::to_string(executionGroupID);
  const bool hasCoverage = IsAlphaTestGroup(executionGroupID);
  std::map<std::string_view, u16> materialFunctionIDs;
  std::map<std::string_view, u16> coverageFunctionIDs;
  for (const CookedMaterialProgram &cooked : plan.programs) {
    if (!RouteForGroup(cooked, executionGroupID))
      continue;
    for (u32 unit = 0; unit < cooked.authoredUnitCount; ++unit) {
      materialFunctionIDs.try_emplace(
          cooked.unitMaterialFunctions[unit],
          static_cast<u16>(materialFunctionIDs.size()));
      if (hasCoverage && !cooked.unitCoverageFunctions[unit].empty())
        coverageFunctionIDs.try_emplace(
            cooked.unitCoverageFunctions[unit],
            static_cast<u16>(coverageFunctionIDs.size()));
    }
    if (cooked.authoredUnitCount == 0) {
      materialFunctionIDs.try_emplace(
          cooked.materialFunction,
          static_cast<u16>(materialFunctionIDs.size()));
      if (hasCoverage && !cooked.materialCoverageFunction.empty())
        coverageFunctionIDs.try_emplace(
            cooked.materialCoverageFunction,
            static_cast<u16>(coverageFunctionIDs.size()));
    }
  }

  source << "\nSurfaceDescription EvaluateMaterialFunction" << suffix
         << "(\n"
            "    uint functionID, MaterialEvaluationContext context)\n"
            "{\n"
            "    switch (functionID)\n"
            "    {\n";
  for (const auto &[function, functionID] : materialFunctionIDs) {
    source << "    case " << functionID << "u: return " << function
           << "(context);\n";
  }
  source << "    default:\n"
            "    {\n"
            "        SurfaceDescription surface = "
            "MakeDefaultSurface(context.input);\n"
            "        surface.albedo = float3(1.0f, 0.0f, 1.0f);\n"
            "        return surface;\n"
            "    }\n"
            "    }\n"
            "}\n";
  if (hasCoverage) {
    source << "\nfloat EvaluateCoverageFunction" << suffix
           << "(\n"
              "    uint functionID, MaterialEvaluationContext context)\n"
              "{\n"
              "    switch (functionID)\n"
              "    {\n";
    for (const auto &[function, functionID] : coverageFunctionIDs)
      source << "    case " << functionID << "u: return " << function
             << "(context);\n";
    source << "    default: return 1.0f;\n"
              "    }\n"
              "}\n";
  }
  source << "\n";

  struct GeneratedUnit {
    u16 materialFunctionID;
    u16 coverageFunctionID;
    u8 textureOffset;
    u8 blendMode;
  };
  std::vector<GeneratedUnit> generatedUnits;
  std::map<u16, std::pair<u32, u32>> programRanges;
  std::set<u16> emittedPrograms;
  for (const CookedMaterialProgram &cooked : plan.programs) {
    const FileFormat::Material::MaterialProgramRoute *route =
        RouteForGroup(cooked, executionGroupID);
    if (!route || !emittedPrograms.emplace(route->groupLocalProgramID).second)
      continue;
    const u32 firstUnit = static_cast<u32>(generatedUnits.size());
    if (cooked.authoredUnitCount == 0) {
      generatedUnits.push_back(
          {materialFunctionIDs.at(cooked.materialFunction),
           !hasCoverage || cooked.materialCoverageFunction.empty()
               ? 0xFFFFu
               : coverageFunctionIDs.at(cooked.materialCoverageFunction),
           0, 0});
    } else {
      for (u32 unit = 0; unit < cooked.authoredUnitCount; ++unit) {
        generatedUnits.push_back(
            {materialFunctionIDs.at(cooked.unitMaterialFunctions[unit]),
             !hasCoverage || cooked.unitCoverageFunctions[unit].empty()
                 ? 0xFFFFu
                 : coverageFunctionIDs.at(cooked.unitCoverageFunctions[unit]),
             cooked.program.units[unit].textureOffset,
             cooked.program.units[unit].blendMode});
      }
    }
    programRanges.emplace(
        route->groupLocalProgramID,
        std::pair(firstUnit,
                  static_cast<u32>(generatedUnits.size()) - firstUnit));
  }

  if (!programRanges.empty()) {
    source << "static const uint2 MATERIAL_PROGRAMS" << suffix << "[] =\n{\n";
    for (const auto &[programID, range] : programRanges) {
      source << "    uint2(" << range.first << "u, " << range.second << "u),"
             << " // " << programID << "\n";
    }
    source << "};\n\nstatic const uint4 MATERIAL_UNITS" << suffix
           << "[] =\n{\n";
    for (const GeneratedUnit &unit : generatedUnits) {
      source << "    uint4(" << unit.materialFunctionID << "u, "
             << unit.coverageFunctionID << "u, "
             << static_cast<u32>(unit.textureOffset) << "u, "
             << static_cast<u32>(unit.blendMode) << "u),\n";
    }
    source
        << "};\n\n"
           "SurfaceDescription EvaluateCookedMaterialGroup"
        << suffix
        << "(\n"
           "    uint groupLocalProgramID, MaterialEvaluationContext context)\n"
           "{\n"
           "    uint2 range = MATERIAL_PROGRAMS"
        << suffix
        << "[groupLocalProgramID];\n"
           "    uint4 unit = MATERIAL_UNITS"
        << suffix
        << "[range.x];\n"
           "    context.textureOffset = unit.z;\n"
           "    SurfaceDescription surface = EvaluateMaterialFunction"
        << suffix
        << "(unit.x, context);\n"
           "    if (range.y == 1u) return surface;\n"
           "    for (uint index = 1u; index < range.y; ++index)\n"
           "    {\n"
           "        unit = MATERIAL_UNITS"
        << suffix
        << "[range.x + index];\n"
           "        context.textureOffset = unit.z;\n"
           "        surface = BlendCompatibilityMaterialLayers(surface, "
           "EvaluateMaterialFunction"
        << suffix
        << "(unit.x, context), unit.w);\n"
           "    }\n"
           "    return surface;\n"
           "}\n";
    if (hasCoverage) {
      source << "\nfloat EvaluateCookedMaterialCoverageGroup" << suffix
             << "(\n"
                "    uint groupLocalProgramID, MaterialEvaluationContext "
                "context)\n"
                "{\n"
                "    uint2 range = MATERIAL_PROGRAMS"
             << suffix
             << "[groupLocalProgramID];\n"
                "    uint4 unit = MATERIAL_UNITS"
             << suffix
             << "[range.x];\n"
                "    context.textureOffset = unit.z;\n"
                "    float coverage = EvaluateCoverageFunction"
             << suffix
             << "(unit.y, context);\n"
                "    for (uint index = 1u; index < range.y; ++index)\n"
                "    {\n"
                "        unit = MATERIAL_UNITS"
             << suffix
             << "[range.x + index];\n"
                "        context.textureOffset = unit.z;\n"
                "        coverage = "
                "BlendCompatibilityMaterialLayerCoverage(coverage, "
                "EvaluateCoverageFunction"
             << suffix
             << "(unit.y, context), unit.w);\n"
                "    }\n"
                "    return coverage;\n"
                "}\n";
    }
  } else {
    source << "SurfaceDescription EvaluateCookedMaterialGroup" << suffix
           << "(\n"
              "    uint, MaterialEvaluationContext context)\n"
              "{\n"
              "    SurfaceDescription surface = "
              "MakeDefaultSurface(context.input);\n"
              "    surface.albedo = float3(1.0f, 0.0f, 1.0f);\n"
              "    return surface;\n"
              "}\n";
    if (hasCoverage) {
      source << "\nfloat EvaluateCookedMaterialCoverageGroup" << suffix
             << "(\n"
                "    uint, MaterialEvaluationContext)\n"
                "{\n"
                "    return 1.0f;\n"
                "}\n";
    }
  }
  return source.str();
}

bool AuthoredMaterialShaderGenerator::Generate(
    const std::filesystem::path &directory, const MaterialCookPlan &plan) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error)
    return false;

  std::set<u16> groups;
  for (u16 group = 0; group < FileFormat::Material::ABI::EXECUTION_GROUP_COUNT; ++group) {
    if (std::any_of(plan.programs.begin(), plan.programs.end(),
                    [group](const CookedMaterialProgram &cooked) {
                      return RouteForGroup(cooked, group) != nullptr;
                    }))
      groups.emplace(group);
  }

  for (u16 group : groups) {
    const std::filesystem::path path =
        directory / ("MaterialGroup" + std::to_string(group) + ".inc.slang");
    if (!WriteIfChanged(path, GenerateGroupSource(plan, group)))
      return false;
  }

  if (!WriteIfChanged(directory / "MaterialActiveGroup.inc.slang",
                      GenerateActiveGroupSource(groups)) ||
      !WriteIfChanged(directory / "MaterialAlphaTestGroups.inc.slang",
                      GenerateAlphaTestGroupsSource(groups)))
    return false;

  std::ostringstream resolve;
  resolve << "permutation MATERIAL_COOK_GROUP = [";
  bool firstResolveGroup = true;
  for (u16 group : groups) {
    resolve << (firstResolveGroup ? "" : ", ") << group;
    firstResolveGroup = false;
  }
  resolve << "];\n#include \"Material/MaterialResolve.inc.slang\"\n";
  if (!WriteIfChanged(directory / "MaterialResolve.cs.slang", resolve.str()))
    return false;

  const std::filesystem::path validationPath =
      directory / "MaterialGroups.cs.slang";
  std::ostringstream validation;
  validation << "permutation MATERIAL_COOK_GROUP = [";
  bool firstGroup = true;
  for (u16 group : groups) {
    validation << (firstGroup ? "" : ", ") << group;
    firstGroup = false;
  }
  validation
      << "];\n"
         "// Generated Material group compilation harness. Do not hand edit.\n"
         "#include \"Material/LightingModels.inc.slang\"\n"
         "#include \"Generated/MaterialActiveGroup.inc.slang\"\n\n"
         "[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<uint4> "
         "_materialCookOutput;\n\n"
         "[shader(\"compute\")]\n"
         "[numthreads(1, 1, 1)]\n"
         "void main(uint3 dispatchThreadID : SV_DispatchThreadID)\n"
         "{\n"
         "    MaterialInput input;\n"
         "    input.worldPosition = 0.0f;\n"
         "    input.geometricNormal = float3(0.0f, 1.0f, 0.0f);\n"
         "    input.uv0 = 0.0f;\n"
         "    input.uv0DDX = 0.0f;\n"
         "    input.uv0DDY = 0.0f;\n"
         "    input.frontFacing = true;\n"
         "    MaterialEvaluationContext context;\n"
         "    context.input = input;\n"
         "    context.instance.parameterOffset = 0u;\n"
         "    context.instance.materialIndex = 0u;\n"
         "    context.instance.textureOffset = 0u;\n"
         "    context.instance.textureCount = 0u;\n"
         "    context.instance.flags = 0u;\n"
         "    context.instance.packedClassification = 0u;\n"
         "    context.instance.materialHandle = 0u;\n"
         "    context.instance.samplerOffset = 0u;\n"
         "    context.textureOffset = 0u;\n"
         "    SurfaceDescription surface = EvaluateCookedMaterial(\n"
         "        dispatchThreadID.x, context);\n"
         "    float coverage = 1.0f;\n"
         "#if MATERIAL_COOK_GROUP % MATERIAL_EXECUTION_GROUP_CLASS_COUNT == 2 "
         "|| MATERIAL_COOK_GROUP % MATERIAL_EXECUTION_GROUP_CLASS_COUNT == 3\n"
         "    coverage = EvaluateCookedMaterialCoverage(\n"
         "        dispatchThreadID.x, context);\n"
         "#endif\n"
         "    LightingModelInput lightingInput;\n"
         "    lightingInput.worldPosition = input.worldPosition;\n"
         "    lightingInput.viewDirection = float3(0.0f, 0.0f, 1.0f);\n"
         "    lightingInput.ambientVisibility = 1.0f;\n"
         "    lightingInput.shadowVisibility = 1.0f;\n"
         "    float4 output = float4(EvaluateLightingModel(\n"
         "        0u, surface, lightingInput), coverage);\n"
         "    _materialCookOutput[dispatchThreadID.x] = asuint(output);\n"
         "}\n";
  if (!WriteIfChanged(validationPath, validation.str()))
    return false;
  return MaterialBackendShaderGenerator::Generate(directory, plan);
}
} // namespace MaterialCooking
