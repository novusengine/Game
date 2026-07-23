#ifndef LIGHTING_INCLUDED
#define LIGHTING_INCLUDED

#include "Include/Shadows.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"

struct DirectionalLight
{
    float4 direction;
    float4 color; // a = intensity
    float4 groundAmbientColor; // a = intensity
    float4 skyAmbientColor; // a = intensity
    float4 shadowColor;
};
[[vk::binding(7, PER_PASS)]] StructuredBuffer<DirectionalLight> _directionalLights;

[[vk::binding(8, PER_PASS)]] Texture2D<float> _ambientOcclusion;

DirectionalLight LoadDirectionalLight(uint index)
{
    DirectionalLight light = _directionalLights[index];

    // TODO: Pack better

    return light;
}

float3 ApplyLighting(float3 materialColor, PixelVertexData pixelVertexData, uint4 lightInfo, ShadowSettings shadowSettings)
{
    uint numDirectionalLights = lightInfo.x;

    float3 ambientColor = float3(0.0f, 0.0f, 0.0f);
    float3 directionalColor = float3(0.0f, 0.0f, 0.0f);

    float ambientOcclusion = _ambientOcclusion.Load(int3(pixelVertexData.pixelPos, 0)).r;

    // Directional Light Shadows: clipmap selection, page walk and far fade all live in
    // GetSVSMShadowFactor. Light-independent (the sun direction comes from the SVSM state, not
    // the light), so it samples once outside the loop — it is the most expensive part of the pass
    float shadowFactor = 1.0f;
    if (shadowSettings.enableShadows && numDirectionalLights > 0)
    {
        shadowFactor = GetSVSMShadowFactor(pixelVertexData.worldPos, pixelVertexData.worldNormal, shadowSettings);

        // Sun elevation strength, fades shadows out around dawn/dusk
        shadowFactor = lerp(1.0f, shadowFactor, shadowSettings.strength);
    }

    for (uint i = 0; i < numDirectionalLights; i++)
    {
        DirectionalLight light = LoadDirectionalLight(i);

        // Ambient Light
        float nDotUp = saturate(dot(pixelVertexData.worldNormal, float3(0.0f, 1.0f, 0.0f))); // Dot product between normal and up direction
        float4 lightAmbientColor = lerp(light.groundAmbientColor, light.skyAmbientColor, nDotUp); // Ambient color based on normal
        lightAmbientColor.rgb *= lightAmbientColor.a; // Intensity in alpha channel

        // Ambient Occlusion
        lightAmbientColor.rgb *= ambientOcclusion;
        ambientColor += lightAmbientColor.rgb;

        // Directional Light
        float nDotL = saturate(dot(pixelVertexData.worldNormal, light.direction.xyz)); // Dot product between normal and light direction

        // The directional light fades out with the sun's elevation (shadowSettings.strength is
        // clamp(sunElevationSin / 0.1), same ramp the shadow fade rides): below the horizon a
        // Lambert term would light down-facing surfaces from underneath, ambient/skyband carry
        // the night look instead. Like the shadow lerp below, this assumes light 0 is the sun
        float3 lightColor = light.color.rgb * light.color.a * nDotL * shadowSettings.strength;

        // Directional Light Shadows: shadowColor is a multiplicative tint on the directional
        // term (fully shadowed keeps shadowColor's fraction of it), never a replacement — a
        // constant floor exceeds the Lambert term at low sun/grazing angles and inverts the
        // shadows. Shadowed can never exceed lit, and ambient carries shadowed areas
        if (shadowSettings.enableShadows)
        {
            lightColor *= lerp(light.shadowColor.rgb, float3(1.0f, 1.0f, 1.0f), shadowFactor);
        }

        directionalColor += lightColor;
    }
    
    // Point Lights

    // Combine
    float3 color = materialColor * (ambientColor + directionalColor);

    return color;
}

#endif // LIGHTING_INCLUDED