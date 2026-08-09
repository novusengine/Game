#include "OrbitCamera.h"

#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/Rendering/Camera.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace RenderScenes
{
    namespace
    {
        vec4 EncodePlane(const vec3& position, const vec3& normal)
        {
            const vec3 normalized = glm::normalize(normal);
            return vec4(normalized, glm::dot(normalized, position));
        }
    }

    OrbitCamera::OrbitCamera(RenderResources& resources, RenderView& view) : _resources(&resources), _view(&view)
    {
        Update();
    }

    void OrbitCamera::Orbit(f32 deltaYaw, f32 deltaPitch)
    {
        _yaw += deltaYaw;
        _pitch = glm::clamp(_pitch + deltaPitch, -0.6f, 0.6f);
        Update();
    }

    void OrbitCamera::SetDistance(f32 distance)
    {
        _distance = std::max(distance, 0.01f);
        Update();
    }

    void OrbitCamera::SetTarget(const vec3& target)
    {
        _target = target;
        Update();
    }

    void OrbitCamera::Update()
    {
        const f32 cosPitch = glm::cos(_pitch);
        const vec3 forward = glm::normalize(vec3(glm::sin(_yaw) * cosPitch, glm::sin(_pitch), glm::cos(_yaw) * cosPitch));
        const vec3 position = _target - forward * _distance;
        const vec3 right = glm::normalize(glm::cross(vec3(0.0f, 1.0f, 0.0f), forward));
        const vec3 up = glm::normalize(glm::cross(forward, right));

        mat4x4 viewToWorld(1.0f);
        viewToWorld[0] = vec4(right, 0.0f);
        viewToWorld[1] = vec4(up, 0.0f);
        viewToWorld[2] = vec4(forward, 0.0f);
        viewToWorld[3] = vec4(position, 1.0f);

        Camera& camera = _resources->cameras[_view->GetCameraIndex()];
        const f32 nearClip = 0.01f;
        const f32 farClip = 100.0f;
        const f32 fov = glm::radians(45.0f);
        camera.viewToWorld = viewToWorld;
        camera.worldToView = glm::inverse(viewToWorld);
        camera.viewToClip = glm::perspective(fov, static_cast<f32>(_view->GetDimensions().x) / _view->GetDimensions().y, farClip, nearClip);
        camera.clipToView = glm::inverse(camera.viewToClip);
        camera.worldToClip = camera.viewToClip * camera.worldToView;
        camera.clipToWorld = camera.viewToWorld * camera.clipToView;
        camera.eyePosition = vec4(position, 0.0f);
        camera.eyeRotation = vec4(_pitch, _yaw, 0.0f, 0.0f);
        camera.nearFar = vec4(nearClip, farClip, 0.0f, 0.0f);

        const f32 halfVSide = farClip * glm::tan(fov * 0.5f);
        const f32 halfHSide = halfVSide * static_cast<f32>(_view->GetDimensions().x) / _view->GetDimensions().y;
        const vec3 frontFar = farClip * forward;
        camera.frustum[static_cast<size_t>(FrustumPlane::Near)] = EncodePlane(position + nearClip * forward, forward);
        camera.frustum[static_cast<size_t>(FrustumPlane::Far)] = EncodePlane(position + frontFar, -forward);
        camera.frustum[static_cast<size_t>(FrustumPlane::Right)] = EncodePlane(position, glm::cross(up, frontFar - right * halfHSide));
        camera.frustum[static_cast<size_t>(FrustumPlane::Left)] = EncodePlane(position, glm::cross(frontFar + right * halfHSide, up));
        camera.frustum[static_cast<size_t>(FrustumPlane::Top)] = EncodePlane(position, glm::cross(frontFar - up * halfVSide, right));
        camera.frustum[static_cast<size_t>(FrustumPlane::Bottom)] = EncodePlane(position, glm::cross(right, frontFar + up * halfVSide));
        _resources->cameras.SetDirtyElement(_view->GetCameraIndex());
        _view->MarkDirty();
    }
} // namespace RenderScenes
