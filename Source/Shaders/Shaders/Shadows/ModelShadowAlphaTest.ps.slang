#include "DescriptorSet/Global.inc.slang"
#include "Material/MaterialAuthoring.inc.slang"
#include "Generated/MaterialAlphaTestGroups.inc.slang"
#include "Shadows/SVSM.inc.slang"

struct Constants
{
    uint queueIndex;
    uint frameIndex;
    uint dynamicCaster;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(29, PER_PASS)]] StructuredBuffer<SVSMData> _shadowRasterSVSMData;
[[vk::binding(30, PER_PASS)]] StructuredBuffer<uint> _shadowRasterPageTable;
[[vk::binding(31, PER_PASS)]] RWTexture2D<uint> _shadowRasterPagePool;

struct PSInput
{
    float4 position : SV_Position;
    nointerpolation uint clipmapIndex : TEXCOORD0;
    nointerpolation uint materialInstanceIndex : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
};

[shader("fragment")]
void main(PSInput input, bool frontFacing : SV_IsFrontFace)
{
    uint requiredFlags = _constants.dynamicCaster != 0u ? SVSM_PAGE_RESIDENT :
                                                          SVSM_PAGE_RESIDENT | SVSM_PAGE_DIRTY;
    uint pageEntry = SVSMLoadPageEntry(input.position.xy, input.clipmapIndex, _shadowRasterSVSMData,
                                       _shadowRasterPageTable);
    if ((pageEntry & requiredFlags) != requiredFlags)
        return;

    MaterialInstanceRecord instance = _materialInstances[input.materialInstanceIndex];
    MaterialRecord material = _materials[instance.materialIndex];
    MaterialInput materialInput;
    materialInput.worldPosition = 0.0f;
    materialInput.geometricNormal = float3(0.0f, 1.0f, 0.0f);
    materialInput.vertexColor = 1.0f;
    materialInput.uv0 = input.uv0;
    materialInput.uv0DDX = ddx(input.uv0);
    materialInput.uv0DDY = ddy(input.uv0);
    materialInput.frontFacing = frontFacing;
    MaterialEvaluationContext context;
    context.input = materialInput;
    context.instance = instance;
    context.textureOffset = 0u;
    context.parameterBlockIndex = 0u;

    uint executionGroup = MaterialExecutionGroup(instance);
    uint localProgramID = MaterialGroupLocalProgramID(material, MaterialRasterClass(instance));
    float coverage = EvaluateAlphaTestMaterialCoverage(executionGroup, localProgramID, context);
    if (coverage < 0.0f)
        discard;
    coverage = saturate(coverage);
    float alphaCutoff = asfloat(_materialParameters.Load(instance.parameterOffset + MATERIAL_ALPHA_CUTOFF_OFFSET));
    if (coverage < alphaCutoff)
        discard;

    uint poolPagesPerRow = _constants.dynamicCaster != 0u ?
        _shadowRasterSVSMData[0].configDynamicPoolPagesPerRow :
        _shadowRasterSVSMData[0].configPoolPagesPerRow;
    SVSMWritePageDepthEntry(input.position.xyz, pageEntry, requiredFlags, poolPagesPerRow,
                            _shadowRasterSVSMData, _shadowRasterPagePool);
}
