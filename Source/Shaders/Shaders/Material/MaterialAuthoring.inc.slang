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
};

float4 SampleMaterialTexture(MaterialEvaluationContext context, uint textureSlot)
{
    uint resolvedSlot = context.textureOffset + textureSlot;
    if (resolvedSlot >= context.instance.textureCount)
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    uint textureIndex = _materialTextureIndices[context.instance.textureOffset + resolvedSlot];
    uint samplerID = _materialSamplerIDs[context.instance.samplerOffset + resolvedSlot];
    Texture2D<float4> texture = _materialTextures[NonUniformResourceIndex(textureIndex)];
    if (samplerID == 0u) return texture.SampleGrad(_materialSampler0, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY);
    if (samplerID == 1u) return texture.SampleGrad(_materialSampler1, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY);
    if (samplerID == 2u) return texture.SampleGrad(_materialSampler2, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY);
    return texture.SampleGrad(_materialSampler3, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY);
}

#endif
