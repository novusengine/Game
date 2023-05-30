#include "CameraInfo.h"

#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Singletons/FreeflyingCameraSettings.h"
#include "Game/ECS/Components/Transform.h"
#include "Game/ECS/Components/Camera.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <string>

namespace Editor
{
    CameraInfo::CameraInfo()
        : BaseEditor(GetName(), true)
	{

	}

	void CameraInfo::DrawImGui()
	{
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->gameRegistry;

        entt::registry::context& ctx = registry.ctx();

        ECS::Singletons::ActiveCamera& activeCamera = ctx.at<ECS::Singletons::ActiveCamera>();
        ECS::Singletons::FreeflyingCameraSettings& settings = ctx.at<ECS::Singletons::FreeflyingCameraSettings>();

        ECS::Components::Transform& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);
        ECS::Components::Camera& camera = registry.get<ECS::Components::Camera>(activeCamera.entity);

        // Print position
        if (ImGui::Begin(GetName()))
        {
            quat rotQuat = quat(vec3(glm::radians(camera.pitch), glm::radians(camera.yaw), glm::radians(camera.roll)));

            ImGui::Text("Pos: (%.2f, %.2f, %.2f)", cameraTransform.position.x, cameraTransform.position.y, cameraTransform.position.z);
            ImGui::Text("Speed: %.2f", settings.cameraSpeed);

            ImGui::Separator();

            ImGui::Text("Pitch: %.2f", camera.pitch);
            ImGui::Text("Yaw: %.2f", camera.yaw);
            ImGui::Text("Roll: %.2f", camera.roll);

            vec3 eulerAngles = glm::eulerAngles(rotQuat);
            ImGui::Text("Euler: (%.2f, %.2f, %.2f)", eulerAngles.x, eulerAngles.y, eulerAngles.z);

            ImGui::Separator();

            ImGui::Text("Forward: (%.2f, %.2f, %.2f)", cameraTransform.forward.x, cameraTransform.forward.y, cameraTransform.forward.z);
            ImGui::Text("Right: (%.2f, %.2f, %.2f)", cameraTransform.right.x, cameraTransform.right.y, cameraTransform.right.z);
            ImGui::Text("Up: (%.2f, %.2f, %.2f)", cameraTransform.up.x, cameraTransform.up.y, cameraTransform.up.z);
        }
        ImGui::End();
	}
}