#ifndef MATERIAL_EVALUATION_INCLUDED
#define MATERIAL_EVALUATION_INCLUDED

#include "Model/ModelViewWork.inc.slang"

#define MATERIAL_BASE_COLOR_OFFSET 0u
#define MATERIAL_TEXTURE_OFFSET 16u
#define MATERIAL_LEGACY_UNIT_OFFSET 48u
#define MATERIAL_FLAGS_OFFSET 80u
#define MATERIAL_ALPHA_CUTOFF_OFFSET 84u
#define MATERIAL_MAX_TEXTURES 8u
#define FALLBACK_MATERIAL_PROGRAM_ID 0xFFFFFFFFu

#ifndef MATERIAL_RECORD_BINDING
#define MATERIAL_RECORD_BINDING 24
#define MATERIAL_PARAMETER_BINDING 25
#define MATERIAL_SAMPLER0_BINDING 26
#define MATERIAL_SAMPLER1_BINDING 27
#define MATERIAL_SAMPLER2_BINDING 28
#define MATERIAL_SAMPLER3_BINDING 29
#define MATERIAL_TEXTURE_BINDING 30
#endif // MATERIAL_EVALUATION_INCLUDED

[[vk::binding(MATERIAL_RECORD_BINDING, PER_PASS)]] StructuredBuffer<MaterialRecord> _materials;
[[vk::binding(MATERIAL_PARAMETER_BINDING, PER_PASS)]] ByteAddressBuffer _materialParameters;
[[vk::binding(MATERIAL_SAMPLER0_BINDING, PER_PASS)]] SamplerState _materialSampler0;
[[vk::binding(MATERIAL_SAMPLER1_BINDING, PER_PASS)]] SamplerState _materialSampler1;
[[vk::binding(MATERIAL_SAMPLER2_BINDING, PER_PASS)]] SamplerState _materialSampler2;
[[vk::binding(MATERIAL_SAMPLER3_BINDING, PER_PASS)]] SamplerState _materialSampler3;
[[vk::binding(MATERIAL_TEXTURE_BINDING, PER_PASS)]] Texture2D<float4> _materialTextures[];

struct MaterialParameters
{
    float4 baseColor;
    uint textureIndices[MATERIAL_MAX_TEXTURES];
    uint legacyUnits[MATERIAL_MAX_TEXTURES];
    uint flags;
    float alphaCutoff;
};

MaterialParameters LoadMaterialParameters(uint parameterOffset)
{
    MaterialParameters parameters;
    parameters.baseColor = asfloat(_materialParameters.Load4(parameterOffset + MATERIAL_BASE_COLOR_OFFSET));
    for (uint index = 0u; index < MATERIAL_MAX_TEXTURES; ++index)
    {
        parameters.textureIndices[index] =
            _materialParameters.Load(parameterOffset + MATERIAL_TEXTURE_OFFSET + index * 4u);
        parameters.legacyUnits[index] =
            _materialParameters.Load(parameterOffset + MATERIAL_LEGACY_UNIT_OFFSET + index * 4u);
    }
    parameters.flags = _materialParameters.Load(parameterOffset + MATERIAL_FLAGS_OFFSET);
    parameters.alphaCutoff = asfloat(_materialParameters.Load(parameterOffset + MATERIAL_ALPHA_CUTOFF_OFFSET));
    return parameters;
}

float4 SampleMaterialTexture(uint textureIndex, uint samplerID, float2 uv, float2 uvDDX, float2 uvDDY)
{
    Texture2D<float4> texture = _materialTextures[NonUniformResourceIndex(textureIndex)];
    if (samplerID == 0u) return texture.SampleGrad(_materialSampler0, uv, uvDDX, uvDDY);
    if (samplerID == 1u) return texture.SampleGrad(_materialSampler1, uv, uvDDX, uvDDY);
    if (samplerID == 2u) return texture.SampleGrad(_materialSampler2, uv, uvDDX, uvDDY);
    return texture.SampleGrad(_materialSampler3, uv, uvDDX, uvDDY);
}

float4 EvaluateMaterial(MaterialRecord material, MaterialInstanceRecord instance,
                        MaterialParameters parameters, float2 uv, float2 uvDDX, float2 uvDDY)
{
    uint textureCount = min((parameters.legacyUnits[0] >> 16u) & 0xFu, MATERIAL_MAX_TEXTURES);
    if (material.programID == FALLBACK_MATERIAL_PROGRAM_ID)
        textureCount = 1u;

    float4 color = parameters.baseColor;
    if (textureCount > 0u)
    {
        uint samplerID = instance.packedSamplerIDs & 0x3u;
        color *= SampleMaterialTexture(parameters.textureIndices[0], samplerID, uv, uvDDX, uvDDY);
    }

    // Placeholder mapping retains the authored shader, layer, texture-count, and low unit flags until compiled
    // material programs replace this bounded interpretation.
    for (uint unit = 1u; unit < textureCount; ++unit)
    {
        uint legacyUnit = parameters.legacyUnits[unit];
        uint authoredShaderID = legacyUnit & 0xFFFFu;
        uint layer = (legacyUnit >> 20u) & 0xFFu;
        uint unitFlags = legacyUnit >> 28u;
        uint samplerID = (instance.packedSamplerIDs >> (unit * 2u)) & 0x3u;
        float4 sample = SampleMaterialTexture(parameters.textureIndices[unit], samplerID, uv, uvDDX, uvDDY);
        bool additive = ((authoredShaderID ^ unitFlags) & 1u) != 0u;
        color = additive ? float4(color.rgb + sample.rgb * sample.a, color.a) :
                           lerp(color, color * sample, saturate(sample.a + float(layer != 0u) * 0.25f));
    }
    return color;
}

bool MaterialCoverage(float alpha, MaterialParameters parameters)
{
    return alpha >= parameters.alphaCutoff;
}

#endif
