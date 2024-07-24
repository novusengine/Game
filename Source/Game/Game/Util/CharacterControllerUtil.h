#pragma once
#include <Base/Types.h>

namespace JPH
{
    class BodyFilter;
    class BroadPhaseLayerFilter;
    class CharacterVirtual;
    struct ExtendedUpdateSettings;
    class ObjectLayerFilter;
    class ShapeFilter;
    class TempAllocator;
    class Vec3;

}

namespace Util::CharacterController
{
    struct UpdateSettings
    {
        vec3 mStickToFloorStepDown{ 0, -1.0f, 0 };									        ///< See StickToFloor inStepDown parameter. Can be zero to turn off.
        vec3 mWalkStairsStepUp{ 0, 0.4f, 0 };										        ///< See WalkStairs inStepUp parameter. Can be zero to turn off.
        f32	 mWalkStairsMinStepForward{ 0.02f };									        ///< See WalkStairs inStepForward parameter. Note that the parameter only indicates a magnitude, direction is taken from current velocity.
        f32	 mWalkStairsStepForwardTest{ 0.1f };									        ///< See WalkStairs inStepForwardTest parameter. Note that the parameter only indicates a magnitude, direction is taken from current velocity.
        f32	 mWalkStairsCosAngleForwardContact{ glm::cos(glm::radians(30.0f)) }; ///< Cos(angle) where angle is the maximum angle between the ground normal in the horizontal plane and the character forward vector where we're willing to adjust the step forward test towards the contact normal.
        vec3 mWalkStairsStepDownExtra{ vec3(0.0f)};								            ///< See WalkStairs inStepDownExtra
    };

    void Update(JPH::CharacterVirtual* characterVirtual, f32 deltaTime, JPH::Vec3& inGravity, const UpdateSettings& inSettings, const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter, const JPH::ObjectLayerFilter& inObjectLayerFilter, const JPH::BodyFilter& inBodyFilter, const JPH::ShapeFilter& inShapeFilter, JPH::TempAllocator& inAllocator);
}