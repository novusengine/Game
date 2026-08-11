#ifndef LIGHTING_MODELS_INCLUDED
#define LIGHTING_MODELS_INCLUDED

#include "Material/MaterialContract.inc.slang"

#define LIGHTING_MODEL_STANDARD 0u
#define LIGHTING_MODEL_UNLIT 1u

float3 EvaluateStandardLighting(SurfaceDescription surface, LightingModelInput input)
{
    float3 normal = UnpackSurfaceNormal(surface.packedNormal);
    float nDotL = saturate(dot(normal, input.lightDirection));
    float3 ambient = input.ambientColor * input.ambientVisibility;
    float3 directional = input.lightColor * nDotL * input.lightStrength;
    directional *= lerp(input.shadowColor, 1.0f, input.shadowVisibility);
    return surface.albedo * (ambient + directional) + surface.emissive * surface.emissiveIntensity;
}

float3 EvaluateUnlitLighting(SurfaceDescription surface, LightingModelInput input)
{
    return surface.albedo + surface.emissive * surface.emissiveIntensity;
}

float3 EvaluateLightingModel(uint lightingModel, SurfaceDescription surface, LightingModelInput input)
{
    if (lightingModel == LIGHTING_MODEL_STANDARD)
        return EvaluateStandardLighting(surface, input);
    return EvaluateUnlitLighting(surface, input);
}

#endif
