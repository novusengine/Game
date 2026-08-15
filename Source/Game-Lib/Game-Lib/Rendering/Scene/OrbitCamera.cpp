#include "OrbitCamera.h"

#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/Rendering/Camera.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>

namespace RenderScenes
{
    namespace
    {
        constexpr f32 ORBIT_CAMERA_VERTICAL_FOV = glm::radians(45.0f);
    }

    void CalculateOrbitCameraPose(const mat4x4& targetMatrix, const vec3& eulerAngles, f32 heightOffset,
                                  f32 distance, vec3& position, quat& rotation)
    {
        const mat4x4 height = glm::translate(mat4x4(1.0f), vec3(0.0f, heightOffset, 0.0f));
        const mat4x4 orbit = glm::eulerAngleYXZ(eulerAngles.y, eulerAngles.x, eulerAngles.z);
        const mat4x4 distanceOffset = glm::translate(mat4x4(1.0f), vec3(0.0f, 0.0f, distance));
        const mat4x4 camera = targetMatrix * height * orbit * distanceOffset;
        position = vec3(camera[3]);
        rotation = glm::normalize(glm::quat_cast(camera));
    }

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

    void OrbitCamera::FrameSphere(const vec3& center, f32 radius, f32 margin)
    {
        const vec2 dimensions = vec2(_view->GetDimensions());
        const f32 aspect = dimensions.x / dimensions.y;
        const f32 verticalHalfFov = ORBIT_CAMERA_VERTICAL_FOV * 0.5f;
        const f32 horizontalHalfFov = glm::atan(glm::tan(verticalHalfFov) * aspect);
        _target = center;
        _distance = std::max(radius * margin / glm::sin(std::min(verticalHalfFov, horizontalHalfFov)), 0.01f);
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
        vec3 position;
        quat ignoredRotation;
        CalculateOrbitCameraPose(glm::translate(mat4x4(1.0f), _target), vec3(_pitch, _yaw + glm::pi<f32>(), 0.0f),
                                 0.0f, _distance, position, ignoredRotation);
        const vec3 forward = glm::normalize(_target - position);
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
        const f32 fov = ORBIT_CAMERA_VERTICAL_FOV;
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
