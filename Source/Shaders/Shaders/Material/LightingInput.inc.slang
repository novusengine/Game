#ifndef LIGHTING_INPUT_INCLUDED
#define LIGHTING_INPUT_INCLUDED

#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Light.inc.slang"
#include "Include/Shadows.inc.slang"
#include "Material/MaterialContract.inc.slang"

[[vk::binding(25, PER_PASS)]] Texture2D<float> _ambientVisibility;

LightingModelInput BuildLightingModelInput(uint2 pixelPosition, uint cameraIndex, float3 worldPosition, float3 worldNormal, float3 faceNormal, uint numDirectionalLights, bool sampleAmbientVisibility, ShadowSettings shadowSettings)
{
    LightingModelInput input;
    input.worldPosition = worldPosition;
    input.viewDirection = normalize(_cameras[cameraIndex].eyePosition.xyz - worldPosition);
    input.lightDirection = float3(0.0f, 1.0f, 0.0f);
    input.ambientColor = 1.0f;
    input.lightColor = 0.0f;
    input.shadowColor = 1.0f;
    input.ambientVisibility = sampleAmbientVisibility ? _ambientVisibility.Load(int3(pixelPosition, 0)).r : 1.0f;
    input.shadowVisibility = 1.0f;
    input.lightStrength = shadowSettings.strength;

    if (numDirectionalLights == 0u)
        return input;

    DirectionalLight light = _directionalLights[0];
    input.lightDirection = normalize(light.direction.xyz);
    float nDotUp = saturate(dot(worldNormal, float3(0.0f, 1.0f, 0.0f)));
    float4 ambient = lerp(light.groundAmbientColor, light.skyAmbientColor, nDotUp);
    input.ambientColor = ambient.rgb * ambient.a;
    input.lightColor = light.color.rgb * light.color.a;
    input.shadowColor = light.shadowColor.rgb;

    if (shadowSettings.enableShadows)
    {
        float visibility = GetSVSMShadowFactor(worldPosition, worldNormal, faceNormal, shadowSettings);
        input.shadowVisibility = lerp(1.0f, visibility, shadowSettings.strength);
    }
    return input;
}

#endif
