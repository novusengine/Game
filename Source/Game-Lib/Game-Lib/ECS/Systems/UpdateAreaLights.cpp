#include "UpdateAreaLights.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/AreaLightInfo.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/DayNightCycle.h"
#include "Game-Lib/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialRenderer.h"
#include "Game-Lib/Rendering/Skybox/SkyboxRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <entt/entt.hpp>
#include <glm/gtc/constants.hpp>

AutoCVar_Int CVAR_SunFullRotation(CVarCategory::Client | CVarCategory::Rendering, "sunFullRotation", "sun does a full rotation per day instead of the authored wobble, the sun sets at night", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowSunUpdateInterval(CVarCategory::Client | CVarCategory::Rendering, "shadowSunUpdateInterval", "game seconds between shadow sun direction updates, a continuously rotating sun re-renders every shadow texel each frame and shimmers", 120.0f);

namespace ECS::Systems
{
    // The shadow sun steps in discrete intervals, the visual sun stays smooth. A continuously
    // rotating light invalidates the whole texel grid every frame, which snapping cannot hide
    f32 GetShadowTimeOfDay(f32 timeOfDay)
    {
        f32 interval = CVAR_ShadowSunUpdateInterval.GetFloat();
        if (interval <= 0.0f)
            return timeOfDay;

        return glm::floor(timeOfDay / interval) * interval;
    }

    vec3 UnpackU32BGRToColor(u32 bgr)
    {
        vec3 result;

        u8 colorR = bgr >> 16;
        u8 colorG = (bgr >> 8) & 0xFF;
        u8 colorB = bgr & 0xFF;

        result.r = colorR / 255.0f;
        result.g = colorG / 255.0f;
        result.b = colorB / 255.0f;

        return result;
    }

    vec3 GetBlendedColor(u32 color1, u32 color2, f32 blend)
    {
        vec3 color1Vec = UnpackU32BGRToColor(color1);
        vec3 color2Vec = UnpackU32BGRToColor(color2);

        return glm::mix(color1Vec, color2Vec, blend);
    }

    AreaLightColorData GetLightColorData(const Singletons::AreaLightInfo& areaLightInfo, const Singletons::DayNightCycle& dayNightCycle, ClientDB::Data* lightParamsStorage, ClientDB::Data* lightDataStorage, const MetaGen::Shared::ClientDB::LightRecord* light)
    {
        AreaLightColorData lightColor;
        if (!light)
            return lightColor;

        u32 lightParamID = light->paramIDs[0];

        if (!lightParamsStorage->Has(lightParamID))
            return lightColor;

        auto& lightParams = lightParamsStorage->Get<MetaGen::Shared::ClientDB::LightParamRecord>(lightParamID);

        if (!areaLightInfo.lightParamIDToLightData.contains(lightParamID))
            return lightColor;

        u32 timeInSecondsAsU32 = static_cast<u32>(dayNightCycle.timeInSeconds);
        const auto& lightDataIDs = areaLightInfo.lightParamIDToLightData.at(lightParamID);
        u32 numLightDataIDs = static_cast<u32>(lightDataIDs.size());

        if (numLightDataIDs == 0)
            return lightColor;

        if (numLightDataIDs == 1)
        {
            u32 lightDataID = lightDataIDs[0];

            if (!lightDataStorage->Has(lightDataID))
                return lightColor;

            auto& lightData = lightDataStorage->Get<MetaGen::Shared::ClientDB::LightDataRecord>(lightDataID);

            lightColor.ambientColor = UnpackU32BGRToColor(lightData.ambientColor);
            lightColor.diffuseColor = UnpackU32BGRToColor(lightData.diffuseColor);
            lightColor.fogColor = UnpackU32BGRToColor(lightData.skyColors[5]);
            lightColor.shadowColor = UnpackU32BGRToColor(lightData.shadowColor);
            lightColor.skybandTopColor = UnpackU32BGRToColor(lightData.skyColors[0]);
            lightColor.skybandMiddleColor = UnpackU32BGRToColor(lightData.skyColors[1]);
            lightColor.skybandBottomColor = UnpackU32BGRToColor(lightData.skyColors[2]);
            lightColor.skybandAboveHorizonColor = UnpackU32BGRToColor(lightData.skyColors[3]);
            lightColor.skybandHorizonColor = UnpackU32BGRToColor(lightData.skyColors[4]);
            lightColor.fogEnd = lightData.fogEnd;
            lightColor.fogScaler = lightData.fogScaler;
        }
        else
        {
            u32 currentLightDataIndex = 0;
            u32 nextLightDataIndex = 0;

            for (u32 i = numLightDataIDs; i > 0; i--)
            {
                u32 lightDataID = lightDataIDs[i - 1];

                if (!lightDataStorage->Has(lightDataID))
                    continue;

                auto& lightData = lightDataStorage->Get<MetaGen::Shared::ClientDB::LightDataRecord>(lightDataID);

                if (lightData.timestamp <= timeInSecondsAsU32)
                {
                    currentLightDataIndex = i - 1;
                    break;
                }
            }

            if (currentLightDataIndex < numLightDataIDs - 1)
                nextLightDataIndex = currentLightDataIndex + 1;

            u32 currentLightDataID = lightDataIDs[currentLightDataIndex];
            u32 nextLightDataID = lightDataIDs[nextLightDataIndex];

            auto& currentLightData = lightDataStorage->Get<MetaGen::Shared::ClientDB::LightDataRecord>(currentLightDataID);
            auto& nextLightData = lightDataStorage->Get<MetaGen::Shared::ClientDB::LightDataRecord>(nextLightDataID);

            u32 currentTimestamp = currentLightData.timestamp;
            u32 nextTimestamp = nextLightData.timestamp;
            f32 timeToTransition = 0.0f;

            if (nextTimestamp < currentTimestamp)
            {
                u32 diff = (static_cast<u32>(Singletons::DayNightCycle::SecondsPerDay) - currentTimestamp) + nextTimestamp;
                timeToTransition = static_cast<f32>(diff);
            }
            else
            {
                timeToTransition = static_cast<f32>(nextTimestamp - currentTimestamp);
            }

            f32 progressIntoCurrent = static_cast<f32>(timeInSecondsAsU32 - currentTimestamp);
            f32 progressTowardsNext = progressIntoCurrent / timeToTransition;

            lightColor.ambientColor = GetBlendedColor(currentLightData.ambientColor, nextLightData.ambientColor, progressTowardsNext);
            lightColor.diffuseColor = GetBlendedColor(currentLightData.diffuseColor, nextLightData.diffuseColor, progressTowardsNext);
            lightColor.fogColor = GetBlendedColor(currentLightData.skyColors[5], nextLightData.skyColors[5], progressTowardsNext);
            lightColor.shadowColor = GetBlendedColor(currentLightData.shadowColor, nextLightData.shadowColor, progressTowardsNext);
            lightColor.skybandTopColor = GetBlendedColor(currentLightData.skyColors[0], nextLightData.skyColors[0], progressTowardsNext);
            lightColor.skybandMiddleColor = GetBlendedColor(currentLightData.skyColors[1], nextLightData.skyColors[1], progressTowardsNext);
            lightColor.skybandBottomColor = GetBlendedColor(currentLightData.skyColors[2], nextLightData.skyColors[2], progressTowardsNext);
            lightColor.skybandAboveHorizonColor = GetBlendedColor(currentLightData.skyColors[3], nextLightData.skyColors[3], progressTowardsNext);
            lightColor.skybandHorizonColor = GetBlendedColor(currentLightData.skyColors[4], nextLightData.skyColors[4], progressTowardsNext);
            lightColor.fogEnd = glm::mix(currentLightData.fogEnd, nextLightData.fogEnd, progressTowardsNext);
            lightColor.fogScaler = glm::mix(currentLightData.fogScaler, nextLightData.fogScaler, progressTowardsNext);
        }

        return lightColor;
    }

    void UpdateAreaLights::Init(entt::registry& registry)
    {
        entt::registry::context& context = registry.ctx();
        auto& areaLightInfo = context.emplace<Singletons::AreaLightInfo>();
        auto& clientDBSingleton = ServiceLocator::GetEnttRegistries()->dbRegistry->ctx().get<Singletons::ClientDBSingleton>();

        auto* lightStorage = clientDBSingleton.Get(ClientDBHash::Light);
        auto* lightParamsStorage = clientDBSingleton.Get(ClientDBHash::LightParams);
        auto* lightDataStorage = clientDBSingleton.Get(ClientDBHash::LightData);

        if (lightStorage)
        {
            u32 numMaps = lightStorage->GetNumRows();

            areaLightInfo.mapIDToLightIDs.clear();
            areaLightInfo.mapIDToLightIDs.reserve(numMaps);

            lightStorage->Each([&](u32 id, const MetaGen::Shared::ClientDB::LightRecord& light) -> bool
            {
                u16 mapID = light.mapID;

                auto& lightIDs = areaLightInfo.mapIDToLightIDs[mapID];

                if (lightIDs.size() == 0)
                    lightIDs.reserve(16);

                lightIDs.push_back(id);
                return true;
            });
        }

        if (lightDataStorage && lightParamsStorage)
        {
            u32 numLightParams = lightParamsStorage->GetNumRows();

            areaLightInfo.lightParamIDToLightData.clear();
            areaLightInfo.lightParamIDToLightData.reserve(numLightParams);

            lightDataStorage->Each([&](u32 id, const MetaGen::Shared::ClientDB::LightDataRecord& lightData) -> bool
            {
                u16 lightParamID = lightData.lightParamID;

                auto& lightDataIDs = areaLightInfo.lightParamIDToLightData[lightParamID];

                if (lightDataIDs.size() == 0)
                    lightDataIDs.reserve(16);

                lightDataIDs.push_back(id);
                return true;
            });
        }
    }
    void UpdateAreaLights::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateAreaLights");

        entt::registry::context& context = registry.ctx();
        auto& activeCamera = context.get<Singletons::ActiveCamera>();
        auto& areaLightInfo = context.get<Singletons::AreaLightInfo>();
        auto& characterSingleton = context.get<Singletons::CharacterSingleton>();
        auto& dayNightCycle = context.get<Singletons::DayNightCycle>();
        auto& freeflyingCameraSettings = context.get<Singletons::FreeflyingCameraSettings>();

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        auto& clientDBSingleton = ServiceLocator::GetEnttRegistries()->dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
        auto* lightStorage = clientDBSingleton.Get(ClientDBHash::Light);
        auto* lightParamsStorage = clientDBSingleton.Get(ClientDBHash::LightParams);
        auto* lightDataStorage = clientDBSingleton.Get(ClientDBHash::LightData);

        if (!lightStorage || !lightParamsStorage || !lightDataStorage)
            return;

        u32 currentMapID = mapLoader->GetCurrentMapID();
        bool forceDefaultLight = currentMapID == std::numeric_limits<u32>().max();
        const auto* defaultLight = &lightStorage->Get<MetaGen::Shared::ClientDB::LightRecord>(1);

        if (!forceDefaultLight)
        {
            vec3 position = vec3(0.0f);

            if (activeCamera.entity == freeflyingCameraSettings.entity)
            {
                auto& cameraTransform = registry.get<Components::Transform>(activeCamera.entity);
                position = cameraTransform.GetWorldPosition();
            }
            else
            {
                auto& characterControllerTransform = registry.get<Components::Transform>(characterSingleton.controllerEntity);
                position = characterControllerTransform.GetWorldPosition();
            }

            areaLightInfo.activeAreaLights.clear();

            if (areaLightInfo.mapIDToLightIDs.contains(currentMapID))
            {
                std::vector<u32>& lightIDs = areaLightInfo.mapIDToLightIDs[currentMapID];

                for (u16 lightID : lightIDs)
                {
                    const auto& light = lightStorage->Get<MetaGen::Shared::ClientDB::LightRecord>(lightID);

                    const vec3& lightPosition = light.position;
                    if (lightPosition.x == 0 && lightPosition.y == 0 && lightPosition.z == 0)
                    {
                        defaultLight = &light;
                        continue;
                    }

                    f32 distanceToLight = glm::distance(position, lightPosition);
                    if (distanceToLight > light.fallOff.y)
                        continue;

                    AreaLightData& areaLightData = areaLightInfo.activeAreaLights.emplace_back();

                    areaLightData.lightId = lightID;
                    areaLightData.fallOff = light.fallOff;
                    areaLightData.distanceToCenter = distanceToLight;
                    areaLightData.colorData = GetLightColorData(areaLightInfo, dayNightCycle, lightParamsStorage, lightDataStorage, &light);
                }
            }
        }

        std::sort(areaLightInfo.activeAreaLights.begin(), areaLightInfo.activeAreaLights.end(), [](AreaLightData a, AreaLightData b) { return a.distanceToCenter > b.distanceToCenter; });

        AreaLightColorData lightColor = GetLightColorData(areaLightInfo, dayNightCycle, lightParamsStorage, lightDataStorage, defaultLight);

        for (const AreaLightData& areaLightData : areaLightInfo.activeAreaLights)
        {
            f32 lengthOfFallOff = areaLightData.fallOff.y - areaLightData.fallOff.x;
            f32 val = (areaLightData.fallOff.y - areaLightData.distanceToCenter) / lengthOfFallOff;

            // Check if We are inside the inner radius of the light
            if (areaLightData.distanceToCenter <= areaLightData.fallOff.x)
                val = 1.0f;

            lightColor.ambientColor = glm::mix(lightColor.ambientColor, areaLightData.colorData.ambientColor, val);
            lightColor.diffuseColor = glm::mix(lightColor.diffuseColor, areaLightData.colorData.diffuseColor, val);
            lightColor.fogColor = glm::mix(lightColor.fogColor, areaLightData.colorData.fogColor, val);
            lightColor.shadowColor = glm::mix(lightColor.shadowColor, areaLightData.colorData.shadowColor, val);

            lightColor.skybandTopColor = glm::mix(lightColor.skybandTopColor, areaLightData.colorData.skybandTopColor, val);
            lightColor.skybandMiddleColor = glm::mix(lightColor.skybandMiddleColor, areaLightData.colorData.skybandMiddleColor, val);
            lightColor.skybandBottomColor = glm::mix(lightColor.skybandBottomColor, areaLightData.colorData.skybandBottomColor, val);
            lightColor.skybandAboveHorizonColor = glm::mix(lightColor.skybandAboveHorizonColor, areaLightData.colorData.skybandAboveHorizonColor, val);
            lightColor.skybandHorizonColor = glm::mix(lightColor.skybandHorizonColor, areaLightData.colorData.skybandHorizonColor, val);

            lightColor.shallowOceanColor = glm::mix(lightColor.shallowOceanColor, areaLightData.colorData.shallowOceanColor, val);
            lightColor.deepOceanColor = glm::mix(lightColor.deepOceanColor, areaLightData.colorData.deepOceanColor, val);
            lightColor.shallowRiverColor = glm::mix(lightColor.shallowRiverColor, areaLightData.colorData.shallowRiverColor, val);
            lightColor.deepRiverColor = glm::mix(lightColor.deepRiverColor, areaLightData.colorData.deepRiverColor, val);

            lightColor.fogEnd = glm::mix(lightColor.fogEnd, areaLightData.colorData.fogEnd, val);
            lightColor.fogScaler = glm::mix(lightColor.fogScaler, areaLightData.colorData.fogScaler, val);
        }

        areaLightInfo.finalColorData = lightColor;

        MaterialRenderer* materialRenderer = ServiceLocator::GetGameRenderer()->GetMaterialRenderer();
        
        vec3 direction = GetLightDirection(dayNightCycle.GetTimeInSecondsF32());
        const vec3& diffuseColor = areaLightInfo.finalColorData.diffuseColor;
        const vec3& ambientColor = areaLightInfo.finalColorData.ambientColor;
        const vec3& shadowColor = areaLightInfo.finalColorData.shadowColor; // Per-area authored tint, multiplied onto the shadowed directional term

        constexpr f32 diffuseIntensity = 0.7f;
        constexpr f32 ambientIntensity = 1.1f;
        
        if (!materialRenderer->SetDirectionalLight(0, direction, diffuseColor, diffuseIntensity, ambientColor, ambientIntensity, ambientColor, ambientIntensity, shadowColor))
        {
            materialRenderer->AddDirectionalLight(direction, diffuseColor, diffuseIntensity, ambientColor, ambientIntensity, ambientColor, ambientIntensity, shadowColor);
        }
        
        SkyboxRenderer* skyboxRenderer = ServiceLocator::GetGameRenderer()->GetSkyboxRenderer();
        skyboxRenderer->SetSkybandColors(areaLightInfo.finalColorData.skybandTopColor, areaLightInfo.finalColorData.skybandMiddleColor, areaLightInfo.finalColorData.skybandBottomColor, areaLightInfo.finalColorData.skybandAboveHorizonColor, areaLightInfo.finalColorData.skybandHorizonColor);
        skyboxRenderer->SetSunDirection(direction); // direction points toward the sun

        // Fade shadows out as the sun approaches the horizon, below it the shadow views would project the underside of the world
        f32 sunElevationSin = direction.y; // Positive while the sun is above the horizon
        f32 shadowStrength = glm::clamp(sunElevationSin / 0.1f, 0.0f, 1.0f);
        *CVarSystem::Get()->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowStrength"_h) = shadowStrength;

        *CVarSystem::Get()->GetVecFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "fogColor"_h) = vec4(areaLightInfo.finalColorData.fogColor, 1.0f);
        *CVarSystem::Get()->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "fogBlendBegin"_h) = areaLightInfo.finalColorData.fogEnd * areaLightInfo.finalColorData.fogScaler;
        *CVarSystem::Get()->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "fogBlendEnd"_h) = areaLightInfo.finalColorData.fogEnd;
    }

    struct CurveKey
    {
    public:
        f32 time;
        f32 value;
    };

    template<std::size_t KeyCount>
    f32 SampleCyclicLinearCurve(const std::array<CurveKey, KeyCount>& keys, const f32 normalizedTime)
    {
        static_assert(KeyCount > 0);

        const f32 time = glm::clamp(normalizedTime, 0.0f, 1.0f);

        std::size_t nextKeyIndex = 0;
        while (nextKeyIndex < KeyCount && time > keys[nextKeyIndex].time)
        {
            ++nextKeyIndex;
        }

        std::size_t previousKeyIndex;
        if (nextKeyIndex == 0 || nextKeyIndex == KeyCount)
        {
            nextKeyIndex = 0;
            previousKeyIndex = KeyCount - 1;
        }
        else
        {
            previousKeyIndex = nextKeyIndex - 1;
        }

        const CurveKey& previousKey = keys[previousKeyIndex];
        const CurveKey& nextKey = keys[nextKeyIndex];

        f32 interval = nextKey.time - previousKey.time;
        if (interval < 0.0f)
        {
            interval += 1.0f;
        }

        if (glm::abs(interval) < 0.001f)
        {
            return previousKey.value;
        }

        f32 elapsed = time - previousKey.time;
        if (elapsed < 0.0f)
        {
            elapsed += 1.0f;
        }

        const f32 a = previousKey.value;
        const f32 b = nextKey.value;
        const f32 interpolation = elapsed / interval;
        return std::lerp(a, b, interpolation);
    }

    f32 WrapNormalizedTime(const f32 normalizedTime)
    {
        return normalizedTime - glm::floor(normalizedTime);
    }

    glm::vec3 CalculateDirectionToSun(const f32 normalizedTimeOfDay, const bool useFullRotation)
    {

        f32 theta = 0.0f;
        f32 phi = 0.0f;

        if (useFullRotation)
        {
            const f32 time = WrapNormalizedTime(normalizedTimeOfDay);

            // 00:00 = directly below
            // 06:00 = horizon
            // 12:00 = directly above
            // 18:00 = opposite horizon
            theta = glm::pi<f32>() - time * glm::two_pi<f32>();

            // Keeps the original 45-degree northeast/southwest orbital plane.
            phi = glm::quarter_pi<f32>();
        }
        else
        {
            constexpr std::array<CurveKey, 5> sunThetaCurve =
            { {
                { 0.2292f, 1.7453f },
                { 0.4965f, 0.0873f },
                { 0.5000f, 0.0873f },
                { 0.5035f, 0.0873f },
                { 0.8958f, 1.7453f }
            } };

            constexpr std::array<CurveKey, 3> sunPhiCurve =
            { {
                { 0.2292f, 0.7854f },
                { 0.5000f, 0.7854f },
                { 0.8958f, 0.7854f }
            } };

            theta = SampleCyclicLinearCurve(sunThetaCurve, normalizedTimeOfDay);
            phi = SampleCyclicLinearCurve(sunPhiCurve, normalizedTimeOfDay);
        }

        const f32 sinTheta = glm::sin(theta);
        const f32 cosTheta = glm::cos(theta);
        const f32 sinPhi = glm::sin(phi);
        const f32 cosPhi = glm::cos(phi);

        const glm::vec3 directionToSun(-sinPhi * sinTheta, cosTheta, cosPhi * sinTheta);
        return glm::normalize(directionToSun);
    }

    vec3 UpdateAreaLights::GetLightDirection(f32 timeOfDay)
    {
        f32 progress = timeOfDay / 86400.0f;
        f32 progressDayAndNight = glm::clamp(progress, 0.0f, 1.0f);

        const glm::vec3 result = CalculateDirectionToSun(progressDayAndNight, CVAR_SunFullRotation.Get());
        return result;
    }
}