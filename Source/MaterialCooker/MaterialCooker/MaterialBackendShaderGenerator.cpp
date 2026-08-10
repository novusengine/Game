#include "MaterialBackendShaderGenerator.h"

#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace
{
    using namespace MaterialCooking;

    std::set<u16> CollectGroups(const MaterialCookPlan& plan, bool coverageOnly)
    {
        std::set<u16> groups;
        for (const CookedMaterialProgram& cooked : plan.programs)
        {
            for (const FileFormat::Material::MaterialProgramRoute& route : cooked.rasterRoutes)
            {
                const auto groupClass = FileFormat::Material::ABI::GetExecutionGroupClass(
                    route.executionGroupID);
                const bool isCoverageGroup = groupClass == FileFormat::Material::ABI::ExecutionGroup::AlphaTestSimple || groupClass == FileFormat::Material::ABI::ExecutionGroup::AlphaTestLayered;
                if (!coverageOnly || isCoverageGroup)
                    groups.emplace(route.executionGroupID);
            }
        }
        return groups;
    }

    void WriteGroupSelection(std::ostringstream& source, const std::set<u16>& groups)
    {
        source << "permutation MATERIAL_COOK_GROUP = [";
        bool firstGroup = true;
        for (u16 group : groups)
        {
            source << (firstGroup ? "" : ", ") << group;
            firstGroup = false;
        }
        source << "];\n"
                  "#include \"Generated/MaterialActiveGroup.inc.slang\"\n\n";
    }

    void WriteFragmentInput(std::ostringstream& source)
    {
        source << "struct MaterialBackendInput\n"
                  "{\n"
                  "    float4 position : SV_Position;\n"
                  "    float3 worldPosition : TEXCOORD0;\n"
                  "    float3 geometricNormal : TEXCOORD1;\n"
                  "    float2 uv0 : TEXCOORD2;\n"
                  "};\n\n"
                  "MaterialEvaluationContext MakeMaterialContext(\n"
                  "    MaterialBackendInput input, bool frontFacing, uint materialInstanceIndex)\n"
                  "{\n"
                  "    MaterialEvaluationContext context;\n"
                  "    context.input.worldPosition = input.worldPosition;\n"
                  "    context.input.geometricNormal = normalize(input.geometricNormal);\n"
                  "    context.input.uv0 = input.uv0;\n"
                  "    context.input.uv0DDX = ddx(input.uv0);\n"
                  "    context.input.uv0DDY = ddy(input.uv0);\n"
                  "    context.input.frontFacing = frontFacing;\n"
                  "    context.instance = _materialInstances[materialInstanceIndex];\n"
                  "    context.textureOffset = 0u;\n"
                  "    return context;\n"
                  "}\n\n";
    }

    bool WriteSource(const std::filesystem::path& path, std::string_view source)
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
        {
            const std::string existing{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            if (existing == source)
                return true;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return output.good();
    }
}

namespace MaterialCooking
{
    std::string MaterialBackendShaderGenerator::GenerateCoverageSource(
        const MaterialCookPlan& plan)
    {
        std::ostringstream source;
        source << "// Generated restricted Material coverage backend. Do not hand edit.\n";
        WriteGroupSelection(source, CollectGroups(plan, true));
        WriteFragmentInput(source);
        source << "struct Constants\n"
                  "{\n"
                  "    uint groupLocalProgramID;\n"
                  "    uint materialInstanceIndex;\n"
                  "};\n"
                  "[[vk::push_constant]] Constants _constants;\n\n"
                  "[shader(\"fragment\")]\n"
                  "float main(MaterialBackendInput input, bool frontFacing : SV_IsFrontFace) : SV_Target0\n"
                  "{\n"
                  "    MaterialEvaluationContext context = MakeMaterialContext(\n"
                  "        input, frontFacing, _constants.materialInstanceIndex);\n"
                  "    return EvaluateCookedMaterialCoverage(\n"
                  "        _constants.groupLocalProgramID, context);\n"
                  "}\n";
        return source.str();
    }

    std::string MaterialBackendShaderGenerator::GenerateForwardSource(
        const MaterialCookPlan& plan)
    {
        std::ostringstream source;
        source << "// Generated universal Material forward backend. Do not hand edit.\n";
        WriteGroupSelection(source, CollectGroups(plan, false));
        source << "#include \"Material/LightingModels.inc.slang\"\n";
        WriteFragmentInput(source);
        source << "struct Constants\n"
                  "{\n"
                  "    uint groupLocalProgramID;\n"
                  "    uint materialInstanceIndex;\n"
                  "    float ambientVisibility;\n"
                  "    float shadowVisibility;\n"
                  "};\n"
                  "[[vk::push_constant]] Constants _constants;\n\n"
                  "[shader(\"fragment\")]\n"
                  "float4 main(MaterialBackendInput input, bool frontFacing : SV_IsFrontFace) : SV_Target0\n"
                  "{\n"
                  "    MaterialEvaluationContext context = MakeMaterialContext(\n"
                  "        input, frontFacing, _constants.materialInstanceIndex);\n"
                  "    SurfaceDescription surface = EvaluateCookedMaterial(\n"
                  "        _constants.groupLocalProgramID, context);\n"
                  "    LightingModelInput lightingInput;\n"
                  "    lightingInput.worldPosition = input.worldPosition;\n"
                  "    lightingInput.viewDirection = 0.0f;\n"
                  "    lightingInput.lightDirection = normalize(float3(0.35f, 0.8f, 0.45f));\n"
                  "    lightingInput.ambientVisibility = _constants.ambientVisibility;\n"
                  "    lightingInput.shadowVisibility = _constants.shadowVisibility;\n"
                  "    float3 color = EvaluateLightingModel(\n"
                  "        MaterialLightingModel(context.instance), surface, lightingInput);\n"
                  "    return float4(color, surface.alpha);\n"
                  "}\n";
        return source.str();
    }

    bool MaterialBackendShaderGenerator::Generate(
        const std::filesystem::path& directory, const MaterialCookPlan& plan)
    {
        return WriteSource(directory / "MaterialGroupsCoverage.ps.slang",
                           GenerateCoverageSource(plan)) &&
               WriteSource(directory / "MaterialGroupsForward.ps.slang",
                           GenerateForwardSource(plan));
    }
}
