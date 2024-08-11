#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

#include <vector>

namespace ECS
{
    struct AreaLightColorData
    {
    public:
        vec3 ambientColor = vec3(0.60f, 0.53f, 0.40f);
        vec3 diffuseColor = vec3(0.41f, 0.51f, 0.60f);
        vec3 fogColor = vec3(0.0f, 0.0f, 0.0f);
        vec3 shadowColor = vec3(0.0f, 0.0f, 0.0f);

        vec3 skybandTopColor = vec3(0.00f, 0.12f, 0.29f);
        vec3 skybandMiddleColor = vec3(0.23f, 0.64f, 0.81f);
        vec3 skybandBottomColor = vec3(0.60f, 0.86f, 0.96f);
        vec3 skybandAboveHorizonColor = vec3(0.69f, 0.85f, 0.88f);
        vec3 skybandHorizonColor = vec3(0.71f, 0.71f, 0.71f);

        vec4 shallowOceanColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        vec4 deepOceanColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        vec4 shallowRiverColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        vec4 deepRiverColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);

        f32 fogEnd = 500.0f;
        f32 fogScaler = 0.1f;
    };

    struct AreaLightData
    {
        u32 lightId;
        vec2 fallOff;
        f32 distanceToCenter;

        AreaLightColorData colorData;
    };

    namespace Singletons
    {
        struct AreaLightInfo
        {
        public:
            AreaLightColorData finalColorData;
            std::vector<AreaLightData> activeAreaLights;

            robin_hood::unordered_map<u32, std::vector<u32>> mapIDToLightIDs;
            robin_hood::unordered_map<u32, std::vector<u32>> lightParamIDToLightData;
        };
    }
}