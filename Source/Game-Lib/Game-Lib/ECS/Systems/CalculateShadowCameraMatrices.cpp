#include "CalculateShadowCameraMatrices.h"

#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/DayNightCycle.h"
#include "Game-Lib/ECS/Systems/UpdateAreaLights.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

AutoCVar_Int CVAR_ShadowsStable(CVarCategory::Client | CVarCategory::Rendering, "shadowStable", "stable shadows", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowsFreezeCascades(CVarCategory::Client | CVarCategory::Rendering, "shadowFreezeCascades", "freeze cascade cameras and culling planes for debugging", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ShadowCascadeNum(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum", "number of shadow cascades", 4);
AutoCVar_Float CVAR_ShadowCascadeSplitLambda(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSplitLambda", "split lambda for cascades, between 0.0f and 1.0f", 0.8f);
AutoCVar_Float CVAR_ShadowMaxDistance(CVarCategory::Client | CVarCategory::Rendering, "shadowMaxDistance", "distance the cascades are distributed over, shadows fade out at the end of it", 1000.0f);
AutoCVar_Float CVAR_ShadowCasterMargin(CVarCategory::Client | CVarCategory::Rendering, "shadowCasterMargin", "extends cascade culling toward the sun so far-away casters with long shadows are not culled, depth clamp pancakes them onto the near plane", 2500.0f);
AutoCVar_Float CVAR_ShadowSunUpdateInterval(CVarCategory::Client | CVarCategory::Rendering, "shadowSunUpdateInterval", "game seconds between shadow sun direction updates, a continuously rotating sun re-renders every shadow texel each frame and shimmers", 120.0f);

AutoCVar_Int CVAR_ShadowCascadeTextureSize(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSize", "size of biggest cascade (per side), only applies to cascades created after it is set", 4096);

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

    // Converts a Gribb-Hartmann clip plane row (inside if dot(n, p) + d >= 0) to the
    // encoding the culling shaders expect: vec4(n, dot(n, pointOnPlane)) with normalized inward n
    inline vec4 ExtractPlane(const vec4& row)
    {
        f32 normalLength = glm::length(vec3(row));
        return vec4(vec3(row) / normalLength, -row.w / normalLength);
    }

    void CalculateShadowCameraMatrices::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::CalculateShadowCameraMatrices");

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        Renderer::Renderer* renderer = gameRenderer->GetRenderer();

        RenderResources& renderResources = gameRenderer->GetRenderResources();

        u32 numCascades = CVAR_ShadowCascadeNum.GetU32();
        i32 cascadeTextureSize = CVAR_ShadowCascadeTextureSize.Get();
        bool stableShadows = CVAR_ShadowsStable.Get() == 1;

        // Initialize any new shadow cascades
        // Cascade count can shrink at runtime, we keep the excess cameras and depth images around unused since neither can shrink
        u32 numCamerasNeeded = numCascades + 1; // Main camera + cascades
        u32 numCameras = renderResources.cameras.Count();
        if (numCamerasNeeded > numCameras)
        {
            renderResources.cameras.AddCount(numCamerasNeeded - numCameras);
        }

        while (renderResources.shadowDepthCascades.size() < numCascades)
        {
            u32 cascadeIndex = static_cast<u32>(renderResources.shadowDepthCascades.size());

            // Shadow depth rendertarget
            Renderer::DepthImageDesc shadowDepthDesc;
            shadowDepthDesc.dimensions = vec2(cascadeTextureSize, cascadeTextureSize);
            shadowDepthDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_ABSOLUTE;
            shadowDepthDesc.format = Renderer::DepthImageFormat::D32_FLOAT;
            shadowDepthDesc.sampleCount = Renderer::SampleCount::SAMPLE_COUNT_1;
            shadowDepthDesc.depthClearValue = 0.0f;
            shadowDepthDesc.debugName = "ShadowDepthCascade" + std::to_string(cascadeIndex);

            Renderer::DepthImageID cascadeDepthImage = renderer->CreateDepthImage(shadowDepthDesc);
            renderResources.shadowDepthCascades.push_back(cascadeDepthImage);
        }

        CVarSystem* cvarSystem = CVarSystem::Get();

        // Master toggle, no shadow work at all while disabled (resource growth above stays so enabling works at runtime)
        if (*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled"_h) == 0)
            return;

        const bool useSDSM = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowUseSDSM"_h) != 0;
        const bool validateParity = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMValidateParity"_h) != 0;

        // The GPU fit pass owns the cascade cameras while SDSM is on, the CPU copies go stale.
        // On toggle-off force a full re-upload (the loop below would normally re-dirty every frame,
        // but not while frozen)
        static bool previousUseSDSM = useSDSM;
        if (previousUseSDSM && !useSDSM)
        {
            renderResources.cameras.SetDirtyElements(1, numCascades);
        }
        previousUseSDSM = useSDSM;

        if (useSDSM && !validateParity)
            return;

        if (CVAR_ShadowsFreezeCascades.Get())
            return;

        entt::registry::context& ctx = registry.ctx();
        auto& dayNightCycle = ctx.get<Singletons::DayNightCycle>();

        // Get light settings, the shadow sun steps in discrete intervals
        vec3 lightDirection = UpdateAreaLights::GetLightDirection(GetShadowTimeOfDay(dayNightCycle.GetTimeInSecondsF32()));

        // Get active render camera
        auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

        auto& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);
        auto& camera = registry.get<ECS::Components::Camera>(activeCamera.entity);

        vec3 cameraPos = cameraTransform.GetWorldPosition();
        const mat4x4& cameraViewProj = camera.worldToClip;

        // Calculate frustum split depths and matrices for the shadow map cascades
        f32 cascadeSplitLambda = CVAR_ShadowCascadeSplitLambda.GetFloat();

        f32 cascadeSplits[Renderer::Settings::MAX_SHADOW_CASCADES];

        f32 nearClip = camera.nearClip;
        f32 farClip = camera.farClip;

        f32 clipRange = farClip - nearClip;

        // Distribute the cascades up to shadowMaxDistance instead of the full view distance.
        // The log split is computed from at least 1.0, a tiny near plane makes the log terms
        // vanish and degenerates the distribution to its uniform part
        f32 minZ = Math::Max(nearClip, 1.0f);
        f32 maxZ = Math::Min(nearClip + clipRange, CVAR_ShadowMaxDistance.GetFloat());

        f32 range = maxZ - minZ;
        f32 ratio = maxZ / minZ;

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        for (u32 i = 0; i < numCascades; i++)
        {
            f32 p = (i + 1) / static_cast<f32>(numCascades);
            f32 log = minZ * std::pow(ratio, p);
            f32 uniform = minZ + range * p;
            f32 d = cascadeSplitLambda * (log - uniform) + uniform;

            cascadeSplits[i] = 1.0f - ((d - nearClip) / clipRange);
        }

        // Calculate orthographic projection matrix for each cascade
        f32 lastSplitDist = 1.0f;
        for (u32 i = 0; i < numCascades; i++)
        {
            f32 splitDist = cascadeSplits[i];

            vec3 frustumCorners[8] =
            {
                vec3(-1.0f, 1.0f, 0.0f),
                vec3(1.0f, 1.0f, 0.0f),
                vec3(1.0f, -1.0f, 0.0f),
                vec3(-1.0f, -1.0f, 0.0f),
                vec3(-1.0f, 1.0f, 1.0f),
                vec3(1.0f, 1.0f, 1.0f),
                vec3(1.0f, -1.0f, 1.0f),
                vec3(-1.0f, -1.0f, 1.0f)
            };

            mat4x4 invViewProj = glm::inverse(cameraViewProj);
            for (u32 j = 0; j < 8; j++)
            {
                vec4 invCorner = invViewProj * vec4(frustumCorners[j], 1.0f);
                frustumCorners[j] = invCorner / invCorner.w;
            }

            // Get the corners of the current cascsade slice of the view frustum
            for (u32 j = 0; j < 4; j++)
            {
                vec3 cornerRay = frustumCorners[j + 4] - frustumCorners[j];
                vec3 nearCornerRay = cornerRay * lastSplitDist;
                vec3 farCornerRay = cornerRay * splitDist;

                frustumCorners[j + 4] = frustumCorners[j] + farCornerRay; // TODO this looks sus
                frustumCorners[j] = frustumCorners[j] + nearCornerRay;
            }

            // Get frustum center
            vec3 frustumCenter = vec3(0.0f);

            for (u32 j = 0; j < 8; j++)
            {
                frustumCenter += frustumCorners[j];
            }
            frustumCenter /= 8.0f;

            vec3 upDir = -cameraTransform.GetLocalRight(); // This was GetLeft, unsure about this, double check if there are issues

            vec3 minExtents;
            vec3 maxExtents;
            if (stableShadows)
            {
                // This needs to be constant for it to be stable, fall back to Z when the light is
                // near vertical or lookAt degenerates (sun straight above at noon)
                upDir = vec3(0.0f, 1.0f, 0.0f);
                if (glm::abs(lightDirection.y) > 0.99f)
                {
                    upDir = vec3(0.0f, 0.0f, 1.0f);
                }

                // Calculate the radius of a bounding sphere surrounding the frustum corners
                f32 sphereRadius = 0.0f;
                for (u32 j = 0; j < 8; j++)
                {
                    f32 dist = glm::length(frustumCorners[j] - frustumCenter);
                    sphereRadius = Math::Max(sphereRadius, dist);
                }

                sphereRadius = std::ceil(sphereRadius * 16.0f) / 16.0f;

                maxExtents = vec3(sphereRadius, sphereRadius, sphereRadius);
                minExtents = -maxExtents;
            }
            else
            {
                // Create a temporary view matrix for the light, looking along the direction the light travels
                vec3 lightCameraPos = frustumCenter;
                vec3 lookAt = frustumCenter + lightDirection;
                mat4x4 lightView = glm::lookAt(lightCameraPos, lookAt, upDir);

                // Calculate an AABB around the frustum corners
                vec3 mins = vec3(std::numeric_limits<f32>::max());
                vec3 maxes = vec3(std::numeric_limits<f32>::lowest());
                for (u32 j = 0; j < 8; j++)
                {
                    vec4 corner = lightView * vec4(frustumCorners[j], 1.0f);

                    mins = glm::min(mins, vec3(corner / corner.w));
                    maxes = glm::max(maxes, vec3(corner / corner.w));
                }

                minExtents = mins;
                maxExtents = maxes;

                // Adjust the min/max to accommodate the filtering size
                const u32 fixedFilterKernelSize = 3; // TODO: Figure this out from the MJP sample

                f32 scale = (cascadeTextureSize + fixedFilterKernelSize) / static_cast<f32>(cascadeTextureSize);
                minExtents.x *= scale;
                minExtents.y *= scale;
                maxExtents.x *= scale;
                maxExtents.y *= scale;
            }

            // Get postion of the shadow camera, minExtents.z is negative so this places it on the sun side
            vec3 shadowCameraPos = frustumCenter + lightDirection * minExtents.z;

            // Store the far and near planes
            f32 farPlane = maxExtents.z;
            f32 nearPlane = minExtents.z;

            // Come up with a new orthographic projection matrix for the shadow caster
            mat4x4 projMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, farPlane - nearPlane, 0.0f);
            mat4x4 viewMatrix = glm::lookAt(shadowCameraPos, frustumCenter, upDir);

            if (stableShadows)
            {
                // Create the rounding matrix, by projecting the world-space origin and determining the fractional offset in texel space
                mat4x4 shadowMatrix = projMatrix * viewMatrix;
                vec4 shadowOrigin = vec4(0.0f, 0.0f, 0.0f, 1.0f);
                shadowOrigin = shadowMatrix * shadowOrigin;
                shadowOrigin *= static_cast<f32>(cascadeTextureSize) / 2.0f;

                vec4 roundedOrigin = glm::round(shadowOrigin);
                vec4 roundOffset = roundedOrigin - shadowOrigin;
                roundOffset *= 2.0f / static_cast<f32>(cascadeTextureSize);
                roundOffset.z = 0.0f;
                roundOffset.w = 0.0f;

                projMatrix[3] += roundOffset;
            }

            // Store split distance and matrix in cascade camera
            Camera& cascadeCamera = renderResources.cameras[i + 1]; // +1 because the first camera is the main camera

            cascadeCamera.worldToView = viewMatrix;
            cascadeCamera.viewToClip = projMatrix;

            cascadeCamera.viewToWorld = glm::inverse(cascadeCamera.worldToView);
            cascadeCamera.clipToView = glm::inverse(cascadeCamera.viewToClip);

            cascadeCamera.worldToClip = cascadeCamera.viewToClip * cascadeCamera.worldToView;
            cascadeCamera.clipToWorld = cascadeCamera.viewToWorld * cascadeCamera.clipToView;

            f32 splitDepth = (farClip - (nearClip + splitDist * clipRange));// *-1.0f;
            cascadeCamera.eyePosition = vec4(shadowCameraPos, splitDepth); // w holds split depth

            // Extract world-space frustum planes from the view-projection matrix (Gribb-Hartmann)
            const mat4x4& m = cascadeCamera.worldToClip;
            vec4 row0 = glm::row(m, 0);
            vec4 row1 = glm::row(m, 1);
            vec4 row2 = glm::row(m, 2);
            vec4 row3 = glm::row(m, 3);

            cascadeCamera.frustum[(size_t)FrustumPlane::Left] = ExtractPlane(row3 + row0);
            cascadeCamera.frustum[(size_t)FrustumPlane::Right] = ExtractPlane(row3 - row0);
            cascadeCamera.frustum[(size_t)FrustumPlane::Bottom] = ExtractPlane(row3 + row1);
            cascadeCamera.frustum[(size_t)FrustumPlane::Top] = ExtractPlane(row3 - row1);
            cascadeCamera.frustum[(size_t)FrustumPlane::Near] = ExtractPlane(row3 - row2); // Reversed Z, depth 1 is near
            cascadeCamera.frustum[(size_t)FrustumPlane::Far] = ExtractPlane(row2);

            // The near plane sits at the shadow camera on the sun side. Casters beyond it still render
            // correctly (depth clamp pancakes them), so culling must not reject them
            cascadeCamera.frustum[(size_t)FrustumPlane::Near].w -= CVAR_ShadowCasterMargin.GetFloat();

#if NC_DEBUG
            for (u32 j = 0; j < 6; j++)
            {
                const vec4& plane = cascadeCamera.frustum[j];
                f32 distance = glm::dot(vec3(plane), frustumCenter) - plane.w;
                NC_ASSERT(distance > 0.0f, "CalculateShadowCameraMatrices : Cascade frustum plane {0} does not contain the frustum center", j);
            }
#endif
            if (!useSDSM) // In parity-validation mode the CPU result stays local for comparison, the GPU fit owns the upload
            {
                renderResources.cameras.SetDirtyElement(i+1);
            }

            lastSplitDist = cascadeSplits[i];
        }
    }
}
