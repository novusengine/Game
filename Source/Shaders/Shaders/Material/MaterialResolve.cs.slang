#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelVisibilityReconstruct.inc.slang"

#define MATERIAL_RECORD_BINDING 20
#define MATERIAL_PARAMETER_BINDING 21
#define MATERIAL_SAMPLER0_BINDING 31
#define MATERIAL_SAMPLER1_BINDING 32
#define MATERIAL_SAMPLER2_BINDING 33
#define MATERIAL_SAMPLER3_BINDING 34
#define MATERIAL_TEXTURE_BINDING 35
#include "Material/MaterialEvaluation.inc.slang"

#define MATERIAL_EXECUTION_GROUP_COUNT 6u

struct Constants
{
    float4 renderInfo;
    uint resourceIndex;
    uint tileCapacity;
    uint tilesX;
    uint executionGroup;
    uint debugMode;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(3, PER_PASS)]] RWTexture2D<float4> _materialColor;
[[vk::binding(22, PER_PASS)]] Texture2D<uint> _materialIDs;
[[vk::binding(23, PER_PASS)]] StructuredBuffer<uint> _materialTileQueue0;
[[vk::binding(24, PER_PASS)]] StructuredBuffer<uint> _materialTileQueue1;
[[vk::binding(25, PER_PASS)]] StructuredBuffer<uint> _materialGroupMaterials0;
[[vk::binding(26, PER_PASS)]] StructuredBuffer<uint> _materialGroupMaterials1;
[[vk::binding(27, PER_PASS)]] StructuredBuffer<uint> _materialGroupMaterials2;
[[vk::binding(28, PER_PASS)]] StructuredBuffer<uint> _materialGroupMaterials3;
[[vk::binding(29, PER_PASS)]] StructuredBuffer<uint> _materialGroupMaterials4;
[[vk::binding(30, PER_PASS)]] StructuredBuffer<uint> _materialGroupMaterials5;

uint LoadGroupMaterial(uint group, uint localID)
{
    if (group == 0u) return _materialGroupMaterials0[localID];
    if (group == 1u) return _materialGroupMaterials1[localID];
    if (group == 2u) return _materialGroupMaterials2[localID];
    if (group == 3u) return _materialGroupMaterials3[localID];
    if (group == 4u) return _materialGroupMaterials4[localID];
    return _materialGroupMaterials5[localID];
}

uint MixMaterialID(uint value)
{
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    return value;
}

float3 MaterialIDColor(uint value)
{
    uint mixed = MixMaterialID(value + 0xA511E9B3u);
    return 0.2f + 0.8f * float3(mixed & 255u, (mixed >> 8u) & 255u, (mixed >> 16u) & 255u) / 255.0f;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint queueOffset = _constants.executionGroup * _constants.tileCapacity + groupID.x;
    uint tile = _constants.resourceIndex == 0u ? _materialTileQueue0[queueOffset]
                                               : _materialTileQueue1[queueOffset];
    uint2 tilePosition = uint2(tile % _constants.tilesX, tile / _constants.tilesX);
    uint2 pixelPos = tilePosition * 8u + groupThreadID.xy;
    if (any(pixelPos >= uint2(_constants.renderInfo.xy)))
        return;

    uint localMaterialID = _materialIDs[pixelPos];
    if (localMaterialID == 0xFFFFu)
        return;
    uint visibility = LoadVisibilityBuffer(pixelPos);
    if (!IsModelVisibility(visibility))
        return;

    ReconstructedModelPixel pixel;
    if (!ReconstructModelPixel(UnpackTypedVisibility(visibility), float2(pixelPos) + 0.5f,
                               _constants.renderInfo.xy, 0u, _constants.resourceIndex, pixel))
        return;
    MaterialInstanceRecord instance = _resolvedMaterialInstances[pixel.materialInstanceIndex];
    MaterialRecord material = _materials[instance.materialIndex];
    if (MaterialExecutionGroup(material) != _constants.executionGroup ||
        MaterialGroupLocalID(material) != localMaterialID)
        return;
    uint tableMaterial = LoadGroupMaterial(_constants.executionGroup, localMaterialID);
    if (tableMaterial != instance.materialIndex)
        return;

    MaterialParameters parameters = LoadMaterialParameters(instance.parameterOffset);
    float4 color = EvaluateMaterial(material, instance, parameters, pixel.uv0, pixel.uv0DDX, pixel.uv0DDY);
    float3 normal = pixel.worldNormal;
    if ((material.flags & MATERIAL_TWO_SIDED) != 0u && !pixel.frontFacing)
        normal = -normal;

    uint lightingModel = MaterialLightingModel(material);
    if (lightingModel == 0u)
    {
        float3 lightDirection = normalize(float3(0.35f, 0.8f, 0.45f));
        color.rgb *= 0.25f + 0.75f * saturate(dot(normal, lightDirection));
    }

    if (_constants.debugMode == 1u) color.rgb = MaterialIDColor(instance.materialIndex);
    else if (_constants.debugMode == 2u) color.rgb = MaterialIDColor(material.programID);
    else if (_constants.debugMode == 3u) color.rgb = MaterialIDColor(_constants.executionGroup);
    else if (_constants.debugMode == 4u) color.rgb = MaterialIDColor(lightingModel);
    else if (_constants.debugMode == 5u) color.rgb = MaterialIDColor(parameters.textureIndices[0]);
    _materialColor[pixelPos] = float4(color.rgb, 1.0f);
}
