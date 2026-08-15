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
                  "    nointerpolation uint materialInstanceIndex : TEXCOORD3;\n"
                  "    nointerpolation float opacity : TEXCOORD4;\n"
                  "    nointerpolation uint instanceIndex : TEXCOORD5;\n"
                  "};\n\n"
                  "MaterialEvaluationContext MakeMaterialContext(\n"
                  "    MaterialBackendInput input, bool frontFacing)\n"
                  "{\n"
                  "    MaterialEvaluationContext context;\n"
                  "    context.input.worldPosition = input.worldPosition;\n"
                  "    context.input.geometricNormal = normalize(input.geometricNormal);\n"
                  "    context.input.vertexColor = 1.0f;\n"
                  "    context.input.uv0 = input.uv0;\n"
                  "    context.input.uv0DDX = ddx(input.uv0);\n"
                  "    context.input.uv0DDY = ddy(input.uv0);\n"
                  "    context.input.frontFacing = frontFacing;\n"
                  "    context.instance = _materialInstances[input.materialInstanceIndex];\n"
                  "    context.textureOffset = 0u;\n"
                  "    context.parameterBlockIndex = 0u;\n"
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
                  "        input, frontFacing);\n"
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
        source << "#include \"DescriptorSet/Global.inc.slang\"\n"
                  "#include \"DescriptorSet/Light.inc.slang\"\n"
                  "#include \"Include/OIT.inc.slang\"\n"
                  "#include \"Material/Fog.inc.slang\"\n"
                  "#include \"Material/LightingInput.inc.slang\"\n"
                  "#include \"Material/LightingModels.inc.slang\"\n"
                  "#include \"Material/MaterialEvaluation.inc.slang\"\n";
        WriteFragmentInput(source);
        source << "struct Constants\n"
                  "{\n"
                  "    float4 shadowSettings;\n"
                  "    float4 fogColor;\n"
                  "    float4 fogSettings;\n"
                  "    uint viewIndex;\n"
                  "    uint numDirectionalLights;\n"
                  "    uint shadowsReady;\n"
                  "    uint reserved;\n"
                  "};\n"
                  "[[vk::push_constant]] Constants _constants;\n\n"
                  "struct MaterialForwardOutput\n"
                  "{\n"
                  "    float4 accumulation : SV_Target0;\n"
                  "    float4 revealage : SV_Target1;\n"
                  "};\n\n"
                  "[shader(\"fragment\")]\n"
                  "MaterialForwardOutput main(MaterialBackendInput input, bool frontFacing : SV_IsFrontFace)\n"
                  "{\n"
                  "    MaterialEvaluationContext context = MakeMaterialContext(input, frontFacing);\n"
                  "    MaterialRecord material = _materials[context.instance.materialIndex];\n"
                  "    uint localProgramID = MaterialGroupLocalProgramID(material, 2u);\n"
                  "    SurfaceDescription surface = material.programID == FALLBACK_MATERIAL_PROGRAM_ID\n"
                  "        ? EvaluateFallbackMaterial(context.instance, LoadMaterialParameters(context.instance.parameterOffset), context.input)\n"
                  "        : EvaluateCookedMaterial(localProgramID, context);\n"
                  "    float3 normal = context.input.geometricNormal;\n"
                  "    if ((context.instance.flags & MATERIAL_TWO_SIDED) != 0u && !frontFacing) normal = -normal;\n"
                  "    ShadowSettings shadowSettings;\n"
                  "    shadowSettings.enableShadows = _constants.shadowsReady != 0u;\n"
                  "    shadowSettings.strength = _constants.shadowSettings.x;\n"
                  "    shadowSettings.normalOffset = _constants.shadowSettings.y;\n"
                  "    shadowSettings.svsmConstantBias = _constants.shadowSettings.z;\n"
                  "    LightingModelInput lightingInput = BuildLightingModelInput(uint2(input.position.xy), _constants.viewIndex, input.worldPosition, normal, normal, _constants.numDirectionalLights, false, shadowSettings);\n"
                  "    float4 color = float4(EvaluateLightingModel(MaterialLightingModel(context.instance), surface, lightingInput), EvaluateCookedMaterialOpacity(localProgramID, surface.alpha, input.opacity, surface.albedo));\n"
                  "    color.rgb = ApplyFog(color.rgb, input.worldPosition, _cameras[_constants.viewIndex].eyePosition.xyz, _constants.fogColor.rgb, _constants.fogSettings);\n"
                  "    float viewSpaceDepth = mul(float4(input.worldPosition, 1.0f), _cameras[_constants.viewIndex].worldToView).z;\n"
                  "    float weight = CalculateOITWeight(color, input.position.z, viewSpaceDepth);\n"
                  "    MaterialForwardOutput output;\n"
                  "    output.accumulation = float4(color.rgb * color.a, color.a) * weight;\n"
                  "    output.revealage = color.a;\n"
                  "    return output;\n"
                  "}\n";
        return source.str();
    }

    std::string MaterialBackendShaderGenerator::GenerateDirectForwardSource(const MaterialCookPlan& plan)
    {
        const std::set<u16> groups = CollectGroups(plan, false);
        std::ostringstream source;
        source << "// Generated dynamically routed Material forward backend. Do not hand edit.\n";
        for (u16 group : groups)
            source << "#include \"Generated/MaterialGroup" << group << ".inc.slang\"\n";
        source << "#include \"DescriptorSet/Global.inc.slang\"\n"
                  "#include \"DescriptorSet/Light.inc.slang\"\n"
                  "#include \"Material/LightingInput.inc.slang\"\n"
                  "#include \"Material/LightingModels.inc.slang\"\n"
                  "#include \"Material/MaterialEvaluation.inc.slang\"\n";
        WriteFragmentInput(source);
        source << "SurfaceDescription EvaluateDirectMaterial(uint executionGroup, uint localProgramID, MaterialEvaluationContext context)\n"
                  "{\n"
                  "    switch (executionGroup)\n"
                  "    {\n";
        for (u16 group : groups)
            source << "    case " << group << "u: return EvaluateCookedMaterialGroup" << group << "(localProgramID, context);\n";
        source << "    default:\n"
                  "        SurfaceDescription surface = MakeDefaultSurface(context.input);\n"
                  "        surface.albedo = float3(1.0f, 0.0f, 1.0f);\n"
                  "        return surface;\n"
                  "    }\n"
                  "}\n\n"
                  "float EvaluateDirectCoverage(uint executionGroup, uint localProgramID, MaterialEvaluationContext context)\n"
                  "{\n"
                  "    switch (executionGroup)\n"
                  "    {\n";
        for (u16 group : groups)
        {
            const auto groupClass = FileFormat::Material::ABI::GetExecutionGroupClass(group);
            if (groupClass == FileFormat::Material::ABI::ExecutionGroup::AlphaTestSimple ||
                groupClass == FileFormat::Material::ABI::ExecutionGroup::AlphaTestLayered)
            {
                source << "    case " << group << "u: return EvaluateCookedMaterialCoverageGroup" << group
                       << "(localProgramID, context);\n";
            }
        }
        source << "    default: return 1.0f;\n"
                  "    }\n"
                  "}\n\n"
                  "struct Constants\n"
                  "{\n"
                  "    float4 shadowSettings;\n"
                  "    uint viewIndex;\n"
                  "    uint numDirectionalLights;\n"
                  "    uint shadowsReady;\n"
                  "    uint resourceIndex;\n"
                  "    uint queueIndex;\n"
                  "    uint rasterClass;\n"
                  "    uint reserved0;\n"
                  "    uint reserved1;\n"
                  "};\n"
                  "[[vk::push_constant]] Constants _constants;\n\n"
                  "[shader(\"fragment\")]\n"
                  "float4 main(MaterialBackendInput input, bool frontFacing : SV_IsFrontFace) : SV_Target0\n"
                  "{\n"
                  "    MaterialEvaluationContext context = MakeMaterialContext(input, frontFacing);\n"
                  "    MaterialRecord material = _materials[context.instance.materialIndex];\n"
                  "    uint executionGroup = MaterialExecutionGroup(context.instance);\n"
                  "    uint localProgramID = MaterialGroupLocalProgramID(material, _constants.rasterClass);\n"
                  "    if (_constants.rasterClass == 1u && material.programID != FALLBACK_MATERIAL_PROGRAM_ID)\n"
                  "    {\n"
                  "        float coverage = EvaluateDirectCoverage(executionGroup, localProgramID, context);\n"
                  "        if (coverage < 0.0f) discard;\n"
                  "        float alphaCutoff = asfloat(_materialParameters.Load(context.instance.parameterOffset + MATERIAL_ALPHA_CUTOFF_OFFSET));\n"
                  "        if (saturate(coverage) < alphaCutoff) discard;\n"
                  "    }\n"
                  "    SurfaceDescription surface = material.programID == FALLBACK_MATERIAL_PROGRAM_ID\n"
                  "        ? EvaluateFallbackMaterial(context.instance, LoadMaterialParameters(context.instance.parameterOffset), context.input)\n"
                  "        : EvaluateDirectMaterial(executionGroup, localProgramID, context);\n"
                  "    float3 normal = context.input.geometricNormal;\n"
                  "    if ((context.instance.flags & MATERIAL_TWO_SIDED) != 0u && !frontFacing) normal = -normal;\n"
                  "    ShadowSettings shadowSettings;\n"
                  "    shadowSettings.enableShadows = _constants.shadowsReady != 0u;\n"
                  "    shadowSettings.strength = _constants.shadowSettings.x;\n"
                  "    shadowSettings.normalOffset = _constants.shadowSettings.y;\n"
                  "    shadowSettings.svsmConstantBias = _constants.shadowSettings.z;\n"
                  "    LightingModelInput lightingInput = BuildLightingModelInput(uint2(input.position.xy), _constants.viewIndex, input.worldPosition, normal, normal, _constants.numDirectionalLights, false, shadowSettings);\n"
                  "    return float4(EvaluateLightingModel(MaterialLightingModel(context.instance), surface, lightingInput), 1.0f);\n"
                  "}\n";
        return source.str();
    }

    std::string MaterialBackendShaderGenerator::GenerateSelectionSource(
        const MaterialCookPlan& plan)
    {
        std::ostringstream source;
        source << "// Generated universal Material transparent-selection backend. Do not hand edit.\n";
        WriteGroupSelection(source, CollectGroups(plan, false));
        source << "#include \"Model/ModelGeometry.inc.slang\"\n"
                  "#include \"Material/MaterialEvaluation.inc.slang\"\n";
        WriteFragmentInput(source);
        source << "[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _transparentRasterInstances;\n"
                  "[[vk::binding(15, PER_PASS)]] Texture2D<float> _selectionOpaqueDepth;\n\n"
                  "[shader(\"fragment\")]\n"
                  "float main(MaterialBackendInput input, bool frontFacing : SV_IsFrontFace) : SV_Depth\n"
                  "{\n"
                  "    ModelInstanceRecord modelInstance = _transparentRasterInstances[input.instanceIndex];\n"
                  "    if (modelInstance.highlightIntensity == 1.0f) discard;\n"
                  "    MaterialEvaluationContext context = MakeMaterialContext(input, frontFacing);\n"
                  "    MaterialRecord material = _materials[context.instance.materialIndex];\n"
                  "    uint localProgramID = MaterialGroupLocalProgramID(material, 2u);\n"
                  "    SurfaceDescription surface = material.programID == FALLBACK_MATERIAL_PROGRAM_ID\n"
                  "        ? EvaluateFallbackMaterial(context.instance, LoadMaterialParameters(context.instance.parameterOffset), context.input)\n"
                  "        : EvaluateCookedMaterial(localProgramID, context);\n"
                  "    float opacity = EvaluateCookedMaterialOpacity(localProgramID, surface.alpha, input.opacity, surface.albedo);\n"
                  "    if (opacity <= 0.0001f) discard;\n"
                  "    float opaqueDepth = _selectionOpaqueDepth.Load(uint3(uint2(input.position.xy), 0));\n"
                  "    if (input.position.z < opaqueDepth) discard;\n"
                  "    return input.position.z;\n"
                  "}\n";
        return source.str();
    }

    bool MaterialBackendShaderGenerator::Generate(
        const std::filesystem::path& directory, const MaterialCookPlan& plan)
    {
        return WriteSource(directory / "MaterialGroupsCoverage.ps.slang",
                           GenerateCoverageSource(plan)) &&
               WriteSource(directory / "MaterialGroupsDirectForward.ps.slang",
                           GenerateDirectForwardSource(plan)) &&
               WriteSource(directory / "MaterialGroupsForward.ps.slang",
                           GenerateForwardSource(plan)) &&
               WriteSource(directory / "MaterialGroupsSelection.ps.slang",
                           GenerateSelectionSource(plan));
    }
}
