#include "Include/TypedVisibility.inc.slang"
#include "DescriptorSet/Global.inc.slang"
#include "Material/MaterialEvaluation.inc.slang"

[[vk::binding(23, PER_PASS)]] StructuredBuffer<MaterialInstanceRecord> _modelVisibilityMaterialInstances;

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
uint main(PSInput input) : SV_Target0
{
    if (_constants.queueIndex >= 2u)
    {
        MaterialInstanceRecord instance = _modelVisibilityMaterialInstances[input.materialInstanceIndex];
        MaterialRecord material = _materials[instance.materialIndex];
        MaterialParameters parameters = LoadMaterialParameters(instance.parameterOffset);
        float4 color = EvaluateMaterial(material, instance, parameters, input.uv0, ddx(input.uv0), ddy(input.uv0));
        if (!MaterialCoverage(color.a, parameters))
            discard;
    }
    return PackTypedVisibility(TYPED_VISIBILITY_MODEL, input.recordID, input.triangleID);
}
