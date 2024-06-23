#include "CalculateShadowCameraMatrices.h"

#include "Game/ECS/Components/Camera.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/RenderSettings.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

AutoCVar_Int CVAR_ShadowsStable(CVarCategory::Client | CVarCategory::Rendering, "shadowStable", "stable shadows", 1, CVarFlags::EditCheckbox);

//AutoCVar_Int CVAR_ShadowCascadeNum(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum", "number of shadow cascades", 4);
AutoCVar_Float CVAR_ShadowCascadeSplitLambda(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSplitLambda", "split lambda for cascades, between 0.0f and 1.0f", 0.5f);

AutoCVar_Int CVAR_ShadowCascadeTextureSize(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSize", "size of biggest cascade (per side)", 4096);

namespace ECS::Systems
{
    vec4 EncodePlane(vec3 position, vec3 normal)
    {
        vec3 normalizedNormal = glm::normalize(normal);
        vec4 result = vec4(normalizedNormal, glm::dot(normalizedNormal, position));
        return result;
    }

    void CalculateShadowCameraMatrices::Update(entt::registry& registry, f32 deltaTime)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        Renderer::Renderer* renderer = gameRenderer->GetRenderer();

        RenderResources& renderResources = gameRenderer->GetRenderResources();

		u32 numCascades = 0;// CVAR_ShadowCascadeNum.GetU32();
        i32 cascadeTextureSize = CVAR_ShadowCascadeTextureSize.Get();
		bool stableShadows = CVAR_ShadowsStable.Get() == 1;

        // Initialize any new shadow cascades
        while (numCascades > renderResources.shadowDepthCascades.size())
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

            //renderResources.shadowDescriptorSet.BindArray("_shadowCascadeRTs", cascadeDepthImage, cascadeIndex); // TODO: This needs to be bound in the rendergraph I guess
        }

		// Get light settings
		vec3 lightDirection = vec3(0.0f, -1.0f, 0.0f); // TODO

        // Get active render camera
        entt::registry::context& ctx = registry.ctx();

        ECS::Singletons::ActiveCamera& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();

        ECS::Components::Transform& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);
        ECS::Components::Camera& camera = registry.get<ECS::Components::Camera>(activeCamera.entity);

        vec3 cameraPos = cameraTransform.GetWorldPosition();
        const mat4x4& cameraViewProj = camera.worldToClip;

        // Calculate frustum split depths and matrices for the shadow map cascades
        f32 cascadeSplitLambda = CVAR_ShadowCascadeSplitLambda.GetFloat();

        f32 cascadeSplits[Renderer::Settings::MAX_SHADOW_CASCADES];

        f32 nearClip = camera.nearClip;
        f32 farClip = camera.farClip;

        f32 clipRange = farClip - nearClip;

        f32 minZ = nearClip;
        f32 maxZ = nearClip + clipRange;

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
				// This needs to be constant for it to be stable
				upDir = vec3(0.0f, 1.0f, 0.0f);

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
				// Create a temporary view matrix for the light
				vec3 lightCameraPos = frustumCenter;
				vec3 lookAt = frustumCenter - lightDirection;
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

			vec3 cascadeExtents = maxExtents - minExtents;

			// Get postion of the shadow camera
			vec3 shadowCameraPos = frustumCenter + lightDirection * -minExtents.z;

			// Store the far and near planes
			f32 farPlane = maxExtents.z;
			f32 nearPlane = minExtents.z;

			// Come up with a new orthographic projection matrix for the shadow caster
			mat4x4 projMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, farPlane - nearPlane, -250.0f); // TODO: I had to use this one to fix issues
			//mat4x4 projMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, cascadeExtents.z, 0.0f); // TODO: This was the original, but I have flipped min and max Z because of inversed Z
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
			std::vector<Camera> gpuCameras = renderResources.cameras.Get();
			Camera& cascadeCamera = gpuCameras[i + 1]; // +1 because the first camera is the main camera

			cascadeCamera.worldToView = viewMatrix;
			cascadeCamera.viewToClip = projMatrix;

			cascadeCamera.viewToWorld = glm::inverse(cascadeCamera.worldToView);
			cascadeCamera.clipToView = glm::inverse(cascadeCamera.viewToClip);

			cascadeCamera.worldToClip = cascadeCamera.viewToClip * cascadeCamera.worldToView;
			cascadeCamera.clipToWorld = cascadeCamera.viewToWorld * cascadeCamera.clipToView;

			f32 splitDepth = (farClip - (nearClip + splitDist * clipRange)) * -1.0f;
			cascadeCamera.eyePosition = vec4(shadowCameraPos, splitDepth); // w holds split depth

			vec3 scale;
			quat rotation;
			vec3 translation;
			vec3 skew;
			vec4 perspective;
			glm::decompose(viewMatrix, scale, rotation, translation, skew, perspective);
			vec3 eulerAngles = glm::eulerAngles(rotation);

			f32 pitch = eulerAngles.x;
			f32 yaw = eulerAngles.y;
			f32 roll = eulerAngles.z;

			cascadeCamera.eyeRotation = vec4(roll, pitch, yaw, 0.0f); // Is this order correct?

			// Calculate frustum planes
			glm::vec3 front = glm::vec3(0, 0, 1);
			glm::vec3 right = glm::vec3(1, 0, 0);
			glm::vec3 up = glm::vec3(0, 1, 0);

			front = vec3(viewMatrix * vec4(front, 0.0f));
			right = vec3(viewMatrix * vec4(right, 0.0f));
			up = vec3(viewMatrix * vec4(up, 0.0f));

			cascadeCamera.frustum[(size_t)FrustumPlane::Near] = EncodePlane(shadowCameraPos + front * nearPlane, front);
			cascadeCamera.frustum[(size_t)FrustumPlane::Far] = EncodePlane(shadowCameraPos + front * farPlane , -front);
			cascadeCamera.frustum[(size_t)FrustumPlane::Right] = EncodePlane(shadowCameraPos - right * cascadeExtents.x, right);
			cascadeCamera.frustum[(size_t)FrustumPlane::Left] = EncodePlane(shadowCameraPos + right * cascadeExtents.x, -right);
			cascadeCamera.frustum[(size_t)FrustumPlane::Top] = EncodePlane(shadowCameraPos + up * cascadeExtents.z, -up);
			cascadeCamera.frustum[(size_t)FrustumPlane::Bottom] = EncodePlane(shadowCameraPos - up * cascadeExtents.z, up);
			renderResources.cameras.SetDirtyElement(i+1);

			lastSplitDist = cascadeSplits[i];
		}
    }
}
