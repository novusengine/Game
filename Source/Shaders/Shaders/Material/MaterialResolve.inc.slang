#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelVisibilityReconstruct.inc.slang"
#include "Material/LightingModels.inc.slang"

#include "Material/MaterialEvaluation.inc.slang"
#include "Generated/MaterialActiveGroup.inc.slang"

struct Constants
{
    float4 renderInfo;
    uint resourceIndex;
    uint tileCapacity;
    uint tilesX;
    uint debugMode;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(3, PER_PASS)]] RWTexture2D<float4> _materialColor;
[[vk::binding(22, PER_PASS)]] Texture2D<uint> _materialIDs;
[[vk::binding(23, PER_PASS)]] StructuredBuffer<uint> _materialTileQueue0;
[[vk::binding(24, PER_PASS)]] StructuredBuffer<uint> _materialTileQueue1;

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
    uint queueOffset = MATERIAL_COOK_GROUP * _constants.tileCapacity + groupID.x;
    uint tile = _constants.resourceIndex == 0u ? _materialTileQueue0[queueOffset]
                                               : _materialTileQueue1[queueOffset];
    uint2 tilePosition = uint2(tile % _constants.tilesX, tile / _constants.tilesX);
    uint2 pixelPos = tilePosition * 8u + groupThreadID.xy;
    if (any(pixelPos >= uint2(_constants.renderInfo.xy)))
        return;

    uint materialHandle = _materialIDs[pixelPos];
    if (materialHandle == MATERIAL_INVALID_HANDLE)
        return;
    uint visibility = LoadVisibilityBuffer(pixelPos);
    if (!IsModelVisibility(visibility))
        return;

    ReconstructedModelPixel pixel;
    if (!ReconstructModelPixel(UnpackTypedVisibility(visibility), float2(pixelPos) + 0.5f,
                               _constants.renderInfo.xy, 0u, _constants.resourceIndex, pixel))
        return;
    MaterialInstanceRecord instance = _materialInstances[pixel.materialInstanceIndex];
    MaterialRecord material = _materials[instance.materialIndex];
    if (MaterialExecutionGroup(instance) != MATERIAL_COOK_GROUP ||
        MaterialHandle(instance) != materialHandle)
        return;
    if (materialHandle != instance.materialIndex)
        return;

    float3 normal = pixel.worldNormal;
    if ((instance.flags & MATERIAL_TWO_SIDED) != 0u && !pixel.frontFacing)
        normal = -normal;

    MaterialInput materialInput;
    materialInput.worldPosition = pixel.worldPosition;
    materialInput.geometricNormal = normal;
    materialInput.uv0 = pixel.uv0;
    materialInput.uv0DDX = pixel.uv0DDX;
    materialInput.uv0DDY = pixel.uv0DDY;
    materialInput.frontFacing = pixel.frontFacing;
    MaterialParameters parameters = LoadMaterialParameters(instance.parameterOffset);
    SurfaceDescription surface;
    uint lightingModel;
    if (material.programID == FALLBACK_MATERIAL_PROGRAM_ID)
    {
        surface = EvaluateFallbackMaterial(instance, parameters, materialInput);
        lightingModel = MaterialLightingModel(instance);
    }
    else
    {
        MaterialEvaluationContext context;
        context.input = materialInput;
        context.instance = instance;
        context.textureOffset = 0u;
        uint localProgramID =
            MaterialGroupLocalProgramID(material, MaterialRasterClass(instance));
        surface = EvaluateCookedMaterial(localProgramID, context);
        lightingModel = MaterialLightingModel(instance);
    }

    LightingModelInput lightingInput;
    lightingInput.worldPosition = pixel.worldPosition;
    lightingInput.viewDirection = 0.0f;
    lightingInput.ambientVisibility = 1.0f;
    lightingInput.shadowVisibility = 1.0f;
    float3 color = EvaluateLightingModel(lightingModel, surface, lightingInput);

    if (_constants.debugMode == 1u) color = MaterialIDColor(instance.materialIndex);
    else if (_constants.debugMode == 2u) color = MaterialIDColor(material.programID);
    else if (_constants.debugMode == 3u) color = MaterialIDColor(MATERIAL_COOK_GROUP);
    else if (_constants.debugMode == 4u) color = MaterialIDColor(lightingModel);
    else if (_constants.debugMode == 5u) color = MaterialIDColor(
        _materialTextureIndices[instance.textureOffset]);
    _materialColor[pixelPos] = float4(color, 1.0f);
}
