#pragma once

#include <Base/Types.h>

struct RenderResources;

namespace RenderScenes
{
    class RenderView;

    void CalculateOrbitCameraPose(const mat4x4& targetMatrix, const vec3& eulerAngles, f32 heightOffset,
                                  f32 distance, vec3& position, quat& rotation);

    // Owns CPU-side orbit controls and writes the resulting camera into one View's shared GPU camera slot.
    // It lets interactive offscreen Views reuse camera framing without coupling it to their Scene contents.
    class OrbitCamera
    {
      public:
        OrbitCamera(RenderResources& resources, RenderView& view);

        void Orbit(f32 deltaYaw, f32 deltaPitch);
        void FrameSphere(const vec3& center, f32 radius, f32 margin = 1.1f);
        void SetDistance(f32 distance);
        void SetTarget(const vec3& target);

      private:
        void Update();

        RenderResources* _resources = nullptr;
        RenderView* _view = nullptr;
        vec3 _target = vec3(0.0f);
        f32 _yaw = 0.0f;
        f32 _pitch = 0.05f;
        f32 _distance = 3.0f;
    };
} // namespace RenderScenes
