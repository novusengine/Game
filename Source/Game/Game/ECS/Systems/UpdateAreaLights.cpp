#include "UpdateAreaLights.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/AreaLightInfo.h"
#include "Game/ECS/Singletons/CharacterSingleton.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/ECS/Singletons/DayNightCycle.h"
#include "Game/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Material/MaterialRenderer.h"
#include "Game/Rendering/Skybox/SkyboxRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace ECS::Systems
{
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

    AreaLightColorData GetLightColorData(const Singletons::AreaLightInfo& areaLightInfo, const Singletons::DayNightCycle& dayNightCycle, ClientDB::Storage<ClientDB::Definitions::LightParam>* lightParamsStorage, ClientDB::Storage<ClientDB::Definitions::LightData>* lightDataStorage, const ClientDB::Definitions::Light* light)
    {
        AreaLightColorData lightColor;
        if (!light)
            return lightColor;

        ClientDB::Definitions::LightParam* lightParams = lightParamsStorage->GetRow(light->lightParamsID[0]);
        if (!lightParams)
            return lightColor;

        u32 lightParamID = lightParams->id;
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

            ClientDB::Definitions::LightData* lightData = lightDataStorage->GetRow(lightDataID);
            if (!lightData)
                return lightColor;

            lightColor.ambientColor = UnpackU32BGRToColor(lightData->ambientColor);
            lightColor.diffuseColor = UnpackU32BGRToColor(lightData->diffuseColor);
            lightColor.fogColor = UnpackU32BGRToColor(lightData->skyFogColor);
            lightColor.shadowColor = UnpackU32BGRToColor(lightData->shadowColor);
            lightColor.skybandTopColor = UnpackU32BGRToColor(lightData->skyTopColor);
            lightColor.skybandMiddleColor = UnpackU32BGRToColor(lightData->skyMiddleColor);
            lightColor.skybandBottomColor = UnpackU32BGRToColor(lightData->skyBand1Color);
            lightColor.skybandAboveHorizonColor = UnpackU32BGRToColor(lightData->skyBand2Color);
            lightColor.skybandHorizonColor = UnpackU32BGRToColor(lightData->skySmogColor);
            lightColor.fogEnd = lightData->fogEnd;
            lightColor.fogScaler = lightData->fogScaler;
        }
        else
        {
            u32 currentLightDataIndex = 0;
            u32 nextLightDataIndex = 0;

            for (u32 i = numLightDataIDs; i > 0; i--)
            {
                u32 lightDataID = lightDataIDs[i - 1];
                ClientDB::Definitions::LightData* lightData = lightDataStorage->GetRow(lightDataID);
                if (!lightData)
                    continue;

                if (lightData->timestamp <= timeInSecondsAsU32)
                {
                    currentLightDataIndex = i - 1;
                    break;
                }
            }

            if (currentLightDataIndex < numLightDataIDs - 1)
                nextLightDataIndex = currentLightDataIndex + 1;

            u32 currentLightDataID = lightDataIDs[currentLightDataIndex];
            u32 nextLightDataID = lightDataIDs[nextLightDataIndex];

            ClientDB::Definitions::LightData* currentLightData = lightDataStorage->GetRow(currentLightDataID);
            ClientDB::Definitions::LightData* nextLightData = lightDataStorage->GetRow(nextLightDataID);

            u32 currentTimestamp = currentLightData->timestamp;
            u32 nextTimestamp = nextLightData->timestamp;
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

            lightColor.ambientColor = GetBlendedColor(currentLightData->ambientColor, nextLightData->ambientColor, progressTowardsNext);
            lightColor.diffuseColor = GetBlendedColor(currentLightData->diffuseColor, nextLightData->diffuseColor, progressTowardsNext);
            lightColor.fogColor = GetBlendedColor(currentLightData->skyFogColor, nextLightData->skyFogColor, progressTowardsNext);
            lightColor.shadowColor = GetBlendedColor(currentLightData->shadowColor, nextLightData->shadowColor, progressTowardsNext);
            lightColor.skybandTopColor = GetBlendedColor(currentLightData->skyTopColor, nextLightData->skyTopColor, progressTowardsNext);
            lightColor.skybandMiddleColor = GetBlendedColor(currentLightData->skyMiddleColor, nextLightData->skyMiddleColor, progressTowardsNext);
            lightColor.skybandBottomColor = GetBlendedColor(currentLightData->skyBand1Color, nextLightData->skyBand1Color, progressTowardsNext);
            lightColor.skybandAboveHorizonColor = GetBlendedColor(currentLightData->skyBand2Color, nextLightData->skyBand2Color, progressTowardsNext);
            lightColor.skybandHorizonColor = GetBlendedColor(currentLightData->skySmogColor, nextLightData->skySmogColor, progressTowardsNext);
            lightColor.fogEnd = glm::mix(currentLightData->fogEnd, nextLightData->fogEnd, progressTowardsNext);
            lightColor.fogScaler = glm::mix(currentLightData->fogScaler, nextLightData->fogScaler, progressTowardsNext);
        }

        return lightColor;
    }

    void UpdateAreaLights::Init(entt::registry& registry)
    {
        entt::registry::context& context = registry.ctx();
        auto& areaLightInfo = context.emplace<Singletons::AreaLightInfo>();
        auto& clientDBCollection = context.get<Singletons::ClientDBCollection>();

        auto* lightStorage = clientDBCollection.Get<ClientDB::Definitions::Light>(Singletons::ClientDBHash::Light);
        auto* lightParamsStorage = clientDBCollection.Get<ClientDB::Definitions::LightParam>(Singletons::ClientDBHash::LightParams);
        auto* lightDataStorage = clientDBCollection.Get<ClientDB::Definitions::LightData>(Singletons::ClientDBHash::LightData);

        if (lightStorage)
        {
            u32 numMaps = lightStorage->GetNumRows();

            areaLightInfo.mapIDToLightIDs.clear();
            areaLightInfo.mapIDToLightIDs.reserve(numMaps);

            lightStorage->Each([&](const auto* storage, const ClientDB::Definitions::Light* light)
            {
                u16 mapID = light->mapID;

                auto& lightIDs = areaLightInfo.mapIDToLightIDs[mapID];

                if (lightIDs.size() == 0)
                    lightIDs.reserve(16);

                lightIDs.push_back(light->id);
            });
        }

        if (lightDataStorage && lightParamsStorage)
        {
            u32 numLightParams = lightParamsStorage->GetNumRows();

            areaLightInfo.lightParamIDToLightData.clear();
            areaLightInfo.lightParamIDToLightData.reserve(numLightParams);

            lightDataStorage->Each([&](const auto* storage, const ClientDB::Definitions::LightData* lightData)
            {
                u16 lightParamID = lightData->lightParamID;

                auto& lightDataIDs = areaLightInfo.lightParamIDToLightData[lightParamID];

                if (lightDataIDs.size() == 0)
                    lightDataIDs.reserve(16);

                lightDataIDs.push_back(lightData->id);
            });
        }
    }
    void UpdateAreaLights::Update(entt::registry& registry, f32 deltaTime)
    {
        entt::registry::context& context = registry.ctx();
        auto& activeCamera = context.get<Singletons::ActiveCamera>();
        auto& areaLightInfo = context.get<Singletons::AreaLightInfo>();
        auto& characterSingleton = context.get<Singletons::CharacterSingleton>();
        auto& clientDBCollection = context.get<Singletons::ClientDBCollection>();
        auto& dayNightCycle = context.get<Singletons::DayNightCycle>();
        auto& freeflyingCameraSettings = context.get<Singletons::FreeflyingCameraSettings>();

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        auto* lightStorage = clientDBCollection.Get<ClientDB::Definitions::Light>(Singletons::ClientDBHash::Light);
        auto* lightParamsStorage = clientDBCollection.Get<ClientDB::Definitions::LightParam>(Singletons::ClientDBHash::LightParams);
        auto* lightDataStorage = clientDBCollection.Get<ClientDB::Definitions::LightData>(Singletons::ClientDBHash::LightData);

        if (!lightStorage || !lightParamsStorage || !lightDataStorage)
            return;

        u32 currentMapID = mapLoader->GetCurrentMapID();
        bool forceDefaultLight = currentMapID == std::numeric_limits<u32>().max();
        const ClientDB::Definitions::Light* defaultLight = lightStorage->GetRow(1);

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
                    const auto& light = lightStorage->GetRow(lightID);
                    if (!light)
                        continue;

                    vec3& lightPosition = light->position;
                    if (lightPosition.x == 0 && lightPosition.y == 0 && lightPosition.z == 0)
                    {
                        defaultLight = light;
                        continue;
                    }

                    f32 distanceToLight = glm::distance(position, lightPosition);
                    if (distanceToLight > light->fallOff.y)
                        continue;

                    AreaLightData& areaLightData = areaLightInfo.activeAreaLights.emplace_back();

                    areaLightData.lightId = light->id;
                    areaLightData.fallOff = light->fallOff;
                    areaLightData.distanceToCenter = distanceToLight;
                    areaLightData.colorData = GetLightColorData(areaLightInfo, dayNightCycle, lightParamsStorage, lightDataStorage, light);
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
        
        vec3 direction = GetLightDirection(dayNightCycle.timeInSeconds);
        const vec3& diffuseColor = glm::normalize(areaLightInfo.finalColorData.diffuseColor);
        const vec3& ambientColor = glm::normalize(areaLightInfo.finalColorData.ambientColor);
        vec3 groundAmbientColor = ambientColor * 0.7f;
        vec3 skyAmbientColor = ambientColor * 1.1f;
        vec3 shadowColor = vec3(77.f/255.f, 77.f/255.f, 77.f/255.f);
        
        if (!materialRenderer->SetDirectionalLight(0, direction, diffuseColor, 1.0f, groundAmbientColor, 1.0f, skyAmbientColor, 1.0f, shadowColor))
        {
            materialRenderer->AddDirectionalLight(direction, diffuseColor, 1.0f, groundAmbientColor, 1.0f, skyAmbientColor, 1.0f, shadowColor);
        }
        
        SkyboxRenderer* skyboxRenderer = ServiceLocator::GetGameRenderer()->GetSkyboxRenderer();
        skyboxRenderer->SetSkybandColors(areaLightInfo.finalColorData.skybandTopColor, areaLightInfo.finalColorData.skybandMiddleColor, areaLightInfo.finalColorData.skybandBottomColor, areaLightInfo.finalColorData.skybandAboveHorizonColor, areaLightInfo.finalColorData.skybandHorizonColor);

        *CVarSystem::Get()->GetVecFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "fogColor"_h) = vec4(areaLightInfo.finalColorData.fogColor, 1.0f);     
        *CVarSystem::Get()->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "fogBlendBegin"_h) = areaLightInfo.finalColorData.fogEnd * areaLightInfo.finalColorData.fogScaler;
        *CVarSystem::Get()->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "fogBlendEnd"_h) = areaLightInfo.finalColorData.fogEnd;
    }

    vec3 UpdateAreaLights::GetLightDirection(f32 timeOfDay)
    {
        f32 phiValue = 0;
        const f32 thetaValue = 3.926991f;
        const f32 phiTable[4] =
        {
            2.2165682f,
            1.9198623f,
            2.2165682f,
            1.9198623f
        };

        f32 progressDayAndNight = timeOfDay / 86400.0f;
        u32 currentPhiIndex = static_cast<u32>(progressDayAndNight / 0.25f);
        u32 nextPhiIndex = 0;

        if (currentPhiIndex < 3)
            nextPhiIndex = currentPhiIndex + 1;

        // Lerp between the current value of phi and the next value of phi
        {
            f32 currentTimestamp = currentPhiIndex * 0.25f;
            f32 nextTimestamp = nextPhiIndex * 0.25f;

            f32 transitionTime = 0.25f;
            f32 transitionProgress = (progressDayAndNight / 0.25f) - currentPhiIndex;

            f32 currentPhiValue = phiTable[currentPhiIndex];
            f32 nextPhiValue = phiTable[nextPhiIndex];

            phiValue = glm::mix(currentPhiValue, nextPhiValue, transitionProgress);
        }

        // Convert from Spherical Position to Cartesian coordinates
        f32 sinPhi = glm::sin(phiValue);
        f32 cosPhi = glm::cos(phiValue);

        f32 sinTheta = glm::sin(thetaValue);
        f32 cosTheta = glm::cos(thetaValue);

        f32 lightDirX = sinPhi * cosTheta;
        f32 lightDirZ = sinPhi * sinTheta;
        f32 lightDirY = cosPhi;

        // Can also try (X, Z, -Y)
        return -vec3(lightDirX, lightDirY, lightDirZ);
    }
}