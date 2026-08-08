#ifndef AUTHORED_COMPATIBILITY_MATERIAL_INCLUDED
#define AUTHORED_COMPATIBILITY_MATERIAL_INCLUDED

#define COMPATIBILITY_BASE_COLOR_OFFSET 0u

struct CompatibilityMaterialParameters
{
    float4 baseColor;
};

CompatibilityMaterialParameters LoadCompatibilityMaterialParameters(uint parameterOffset)
{
    CompatibilityMaterialParameters parameters;
    parameters.baseColor = asfloat(
        _materialParameters.Load4(parameterOffset + COMPATIBILITY_BASE_COLOR_OFFSET));
    return parameters;
}

float4 BlendCompatibilityMaterialLayerColor(float4 previous, float4 current, uint blendMode)
{
    float oneMinusSourceAlpha = 1.0f - current.a;
    if (blendMode == 0u || blendMode == 1u) return current;
    if (blendMode == 2u)
        return float4(current.rgb * current.a + previous.rgb * oneMinusSourceAlpha,
                      current.a + previous.a * oneMinusSourceAlpha);
    if (blendMode == 3u) return float4(current.rgb + previous.rgb, current.a);
    if (blendMode == 4u) return float4(current.rgb * current.a + previous.rgb, previous.a);
    if (blendMode == 5u) return current * previous;
    if (blendMode == 6u) return 2.0f * current * previous;
    if (blendMode == 7u) return current + previous * oneMinusSourceAlpha;
    return float4(1.0f, 0.0f, 1.0f, 1.0f);
}

SurfaceDescription BlendCompatibilityMaterialLayers(
    SurfaceDescription previous, SurfaceDescription current, uint blendMode)
{
    float4 color = BlendCompatibilityMaterialLayerColor(
        float4(previous.albedo, previous.alpha),
        float4(current.albedo, current.alpha), blendMode);
    previous.albedo = color.rgb;
    previous.alpha = color.a;
    previous.emissive += current.emissive;
    previous.emissiveIntensity = max(previous.emissiveIntensity, current.emissiveIntensity);
    previous.flags |= current.flags;
    return previous;
}

float BlendCompatibilityMaterialLayerCoverage(float previous, float current, uint blendMode)
{
    return BlendCompatibilityMaterialLayerColor(
        float4(0.0f, 0.0f, 0.0f, previous),
        float4(0.0f, 0.0f, 0.0f, current), blendMode).a;
}

#endif
