#ifndef MATERIAL_FOG_INCLUDED
#define MATERIAL_FOG_INCLUDED

float FogIntensity(float3 worldPosition, float3 eyePosition, float4 fogSettings)
{
    float distanceToEye = distance(eyePosition, worldPosition);
    float range = max(fogSettings.z - fogSettings.y, 0.0001f);
    return saturate((distanceToEye - fogSettings.y) / range) * fogSettings.x;
}

float3 ApplyFog(float3 color, float3 worldPosition, float3 eyePosition, float3 fogColor, float4 fogSettings)
{
    return lerp(color, fogColor, FogIntensity(worldPosition, eyePosition, fogSettings));
}

#endif
