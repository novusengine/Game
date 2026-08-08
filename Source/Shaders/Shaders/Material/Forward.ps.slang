#include "Material/LightingModels.inc.slang"
#include "Material/MaterialEvaluation.inc.slang"

struct ForwardVaryings
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 geometricNormal : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
};

struct Constants
{
    uint materialInstanceIndex;
    float ambientVisibility;
    float shadowVisibility;
};
[[vk::push_constant]] Constants _constants;

[shader("pixel")]
float4 main(ForwardVaryings input, bool frontFacing : SV_IsFrontFace) : SV_Target0
{
    MaterialInstanceRecord instance = _materialInstances[_constants.materialInstanceIndex];

    float3 normal = normalize(input.geometricNormal);
    if ((instance.flags & MATERIAL_TWO_SIDED) != 0u && !frontFacing)
        normal = -normal;

    MaterialInput materialInput;
    materialInput.worldPosition = input.worldPosition;
    materialInput.geometricNormal = normal;
    materialInput.uv0 = input.uv0;
    materialInput.uv0DDX = ddx(input.uv0);
    materialInput.uv0DDY = ddy(input.uv0);
    materialInput.frontFacing = frontFacing;

    MaterialParameters parameters = LoadMaterialParameters(instance.parameterOffset);
    SurfaceDescription surface = EvaluateFallbackMaterial(instance, parameters, materialInput);

    LightingModelInput lightingInput;
    lightingInput.worldPosition = input.worldPosition;
    lightingInput.viewDirection = 0.0f;
    lightingInput.ambientVisibility = _constants.ambientVisibility;
    lightingInput.shadowVisibility = _constants.shadowVisibility;
    float3 color = EvaluateLightingModel(MaterialLightingModel(instance), surface, lightingInput);
    return float4(color, surface.alpha);
}
