#ifndef LIGHTING_INCLUDED
#define LIGHTING_INCLUDED

struct DirectionalLight
{
    float4 direction;
    float4 color; // a = intensity
    float4 groundAmbientColor; // a = intensity
    float4 skyAmbientColor; // a = intensity
};
[[vk::binding(7, PER_PASS)]] StructuredBuffer<DirectionalLight> _directionalLights;

[[vk::binding(8, PER_PASS)]] Texture2D<float> _ambientOcclusion;

DirectionalLight LoadDirectionalLight(uint index)
{
    DirectionalLight light = _directionalLights[index];

    // TODO: Pack better

    return light;
}

float3 ApplyLighting(float3 materialColor, float3 normal, float2 uv, float2 pixelPos, uint4 lightInfo)
{
    uint numDirectionalLights = lightInfo.x;

    float3 ambientColor = float3(0.0f, 0.0f, 0.0f);
    float3 directionalColor = float3(0.0f, 0.0f, 0.0f);

    float ambientOcclusion = _ambientOcclusion.Load(int3(pixelPos, 0)).r;

    for (uint i = 0; i < numDirectionalLights; i++)
    {
        DirectionalLight light = LoadDirectionalLight(i);

        light.groundAmbientColor.rgb = float3(0.4f, 0.4f, 0.4f);
        light.skyAmbientColor.rgb = float3(0.4f, 0.4f, 0.4f);

        // Ambient Light
        float nDotUp = saturate(dot(normal, float3(0.0f, 1.0f, 0.0f))); // Dot product between normal and up direction
        float4 lightAmbientColor = lerp(light.groundAmbientColor, light.skyAmbientColor, nDotUp); // Ambient color based on normal
        ambientColor += lightAmbientColor.rgb * lightAmbientColor.a; // Intensity in alpha channel

        // Ambient Occlusion
        ambientColor *= ambientOcclusion;

        // Directional Light
        float nDotL = saturate(dot(normal, -light.direction.xyz)); // Dot product between normal and light direction
        float3 lightColor = lerp(light.color.rgb, float3(1.0f, 1.0f, 1.0f), nDotL); // Light color based on normal
        directionalColor += lightColor.rgb * light.color.a; // Intensity in alpha channel

        // Directional Light Shadows
        // TODO
    }
    
    // Point Lights

    // Combine
    float3 color = materialColor * (ambientColor + directionalColor);

    return color;
}

#endif // LIGHTING_INCLUDED