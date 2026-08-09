#pragma once

#include <Base/Types.h>

struct RenderResources;

namespace RenderScenes
{
    class RenderView;

    // Owns CPU-side orbit controls and writes the resulting camera into one View's shared GPU camera slot.
    // It lets interactive offscreen Views reuse camera framing without coupling it to their Scene contents.
    class OrbitCamera
    {
      public:
        OrbitCamera(RenderResources& resources, RenderView& view);

        void Orbit(f32 deltaYaw, f32 deltaPitch);
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
