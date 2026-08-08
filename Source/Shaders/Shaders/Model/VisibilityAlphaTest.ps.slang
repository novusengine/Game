#include "Include/TypedVisibility.inc.slang"
#include "DescriptorSet/Global.inc.slang"
#include "Material/MaterialAuthoring.inc.slang"
#include "Generated/MaterialAlphaTestGroups.inc.slang"

struct Constants
{
    uint queueIndex;
    uint viewIndex;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

struct PSInput
{
    nointerpolation uint recordID : TEXCOORD0;
    nointerpolation uint materialInstanceIndex : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
    uint triangleID : SV_PrimitiveID;
};

[shader("fragment")]
uint main(PSInput input, bool frontFacing : SV_IsFrontFace) : SV_Target0
{
    MaterialInstanceRecord instance = _materialInstances[input.materialInstanceIndex];
    MaterialRecord material = _materials[instance.materialIndex];
    MaterialInput materialInput;
    materialInput.worldPosition = 0.0f;
    materialInput.geometricNormal = float3(0.0f, 1.0f, 0.0f);
    materialInput.uv0 = input.uv0;
    materialInput.uv0DDX = ddx(input.uv0);
    materialInput.uv0DDY = ddy(input.uv0);
    materialInput.frontFacing = frontFacing;
    MaterialEvaluationContext context;
    context.input = materialInput;
    context.instance = instance;
    context.textureOffset = 0u;

    uint executionGroup = MaterialExecutionGroup(instance);
    uint localProgramID = MaterialGroupLocalProgramID(material, MaterialRasterClass(instance));
    float coverage = EvaluateAlphaTestMaterialCoverage(
        executionGroup, localProgramID, context);
    if (coverage < 0.0f)
        discard;
    coverage = saturate(coverage);
    float alphaCutoff = asfloat(_materialParameters.Load(
        instance.parameterOffset + MATERIAL_ALPHA_CUTOFF_OFFSET));
    if (coverage < alphaCutoff)
        discard;

    return PackTypedVisibility(TYPED_VISIBILITY_MODEL, input.recordID, input.triangleID);
}
