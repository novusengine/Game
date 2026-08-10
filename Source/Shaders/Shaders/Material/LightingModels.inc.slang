#ifndef LIGHTING_MODELS_INCLUDED
#define LIGHTING_MODELS_INCLUDED

#include "Material/MaterialContract.inc.slang"

#define LIGHTING_MODEL_STANDARD 0u
#define LIGHTING_MODEL_UNLIT 1u

float3 EvaluateStandardLighting(SurfaceDescription surface, LightingModelInput input)
{
    float3 normal = UnpackSurfaceNormal(surface.packedNormal);
    float diffuse = 0.25f + 0.75f * saturate(dot(normal, input.lightDirection));
    diffuse *= input.ambientVisibility * input.shadowVisibility;
    return surface.albedo * diffuse + surface.emissive * surface.emissiveIntensity;
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
