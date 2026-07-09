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

float3 ApplyLighting(float2 uv, float3 materialColor, PixelVertexData pixelVertexData, uint4 lightInfo, ShadowSettings shadowSettings)
{
    uint numDirectionalLights = lightInfo.x;
    uint numCascades = lightInfo.z;

    float3 ambientColor = float3(0.0f, 0.0f, 0.0f);
    float3 directionalColor = float3(0.0f, 0.0f, 0.0f);

    float ambientOcclusion = _ambientOcclusion.Load(int3(pixelVertexData.pixelPos, 0)).r;

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
        float nDotL = saturate(dot(pixelVertexData.worldNormal, -light.direction.xyz)); // Dot product between normal and light direction
        float3 lightColor = lerp(light.color.rgb, float3(1.0f, 1.0f, 1.0f), nDotL); // Light color based on normal
        lightColor *= light.color.a; // Intensity in alpha channel
        
        // Directional Light Shadows
        if (shadowSettings.enableShadows && numCascades > 0)
        {
            shadowSettings.cascadeIndex = GetShadowCascadeIndexFromDepth(pixelVertexData.viewPos.z, numCascades);
            Camera cascadeCamera = _cameras[shadowSettings.cascadeIndex + 1]; // +1 because the first camera is the main camera

            float4 shadowPosition = mul(float4(pixelVertexData.worldPos, 1.0f), cascadeCamera.worldToClip);

            float shadowFactor = GetShadowFactor(uv, shadowPosition, shadowSettings);

            // Fade to unshadowed over the last 15% of the last cascade, beyond it there is no shadow data
            float lastSplitDepth = _cameras[numCascades].eyePosition.w;
            float fadeStartDepth = 0.85f * lastSplitDepth;
            float fade = saturate((pixelVertexData.viewPos.z - fadeStartDepth) / (lastSplitDepth - fadeStartDepth));
            shadowFactor = lerp(shadowFactor, 1.0f, fade);

            lightColor = lerp(light.shadowColor.rgb, lightColor, shadowFactor);
        }

        directionalColor += lightColor;
    }
    
    // Point Lights

    // Combine
    float3 color = materialColor * (ambientColor + (directionalColor * 0.5f));

    return color;
}

#endif // LIGHTING_INCLUDED