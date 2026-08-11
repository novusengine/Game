#ifndef MATERIAL_AUTHORING_INCLUDED
#define MATERIAL_AUTHORING_INCLUDED

#include "Material/MaterialContract.inc.slang"
#include "DescriptorSet/Material.inc.slang"

#define MATERIAL_MAX_TEXTURES 8u

// Authored Materials expose SurfaceDescription EvaluateMaterial(MaterialEvaluationContext).
// Alpha-tested Materials can also expose float EvaluateCoverage(MaterialEvaluationContext);
// coverage is clamped to [0, 1], and the backend applies the MaterialInstance alpha cutoff.
struct MaterialEvaluationContext
{
    MaterialInput input;
    MaterialInstanceRecord instance;
    uint textureOffset;
    uint parameterBlockIndex;
};

float3 RotateMaterialVector(float3 value, float4 rotation)
{
    float3 intermediate = 2.0f * cross(rotation.xyz, value);
    return value + rotation.w * intermediate + cross(rotation.xyz, intermediate);
}

void TransformMaterialUV(MaterialEvaluationContext context, uint textureSlot, inout float2 uv, inout float2 uvDDX, inout float2 uvDDY)
{
    if ((context.instance.flags & MATERIAL_INSTANCE_HAS_TEXTURE_TRANSFORMS) == 0u)
        return;
    uint transformOffset = context.instance.parameterOffset + MATERIAL_TEXTURE_TRANSFORM_OFFSET + textureSlot * MATERIAL_TEXTURE_TRANSFORM_STRIDE;
    float3 translation = asfloat(_materialParameters.Load4(transformOffset + MATERIAL_TEXTURE_TRANSLATION_OFFSET)).xyz;
    float4 rotation = normalize(asfloat(_materialParameters.Load4(transformOffset + MATERIAL_TEXTURE_ROTATION_OFFSET)));
    float3 scale = asfloat(_materialParameters.Load4(transformOffset + MATERIAL_TEXTURE_SCALE_OFFSET)).xyz;
    uv = RotateMaterialVector(float3((uv - 0.5f) * scale.xy, 0.0f), rotation).xy + translation.xy + 0.5f;
    uvDDX = RotateMaterialVector(float3(uvDDX * scale.xy, 0.0f), rotation).xy;
    uvDDY = RotateMaterialVector(float3(uvDDY * scale.xy, 0.0f), rotation).xy;
}

float4 SampleMaterialTexture(MaterialEvaluationContext context, uint textureSlot)
{
    uint resolvedSlot = context.textureOffset + textureSlot;
    if (resolvedSlot >= context.instance.textureCount)
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    uint textureIndex = _materialTextureIndices[context.instance.textureOffset + resolvedSlot];
    uint samplerID = _materialSamplerIDs[context.instance.samplerOffset + resolvedSlot];
    Texture2D<float4> texture = _materialTextures[NonUniformResourceIndex(textureIndex)];
    float2 uv = context.input.uv0;
    float2 uvDDX = context.input.uv0DDX;
    float2 uvDDY = context.input.uv0DDY;
    TransformMaterialUV(context, resolvedSlot, uv, uvDDX, uvDDY);
    if (samplerID == 0u) return texture.SampleGrad(_materialSampler0, uv, uvDDX, uvDDY);
    if (samplerID == 1u) return texture.SampleGrad(_materialSampler1, uv, uvDDX, uvDDY);
    if (samplerID == 2u) return texture.SampleGrad(_materialSampler2, uv, uvDDX, uvDDY);
    return texture.SampleGrad(_materialSampler3, uv, uvDDX, uvDDY);
}

#endif
