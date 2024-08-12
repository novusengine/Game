#include "CharacterControllerUtil.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

void Util::CharacterController::Update(JPH::CharacterVirtual* characterVirtual, f32 deltaTime, JPH::Vec3& inGravity, const UpdateSettings& inSettings, const JPH::BroadPhaseLayerFilter& inBroadPhaseLayerFilter, const JPH::ObjectLayerFilter& inObjectLayerFilter, const JPH::BodyFilter& inBodyFilter, const JPH::ShapeFilter& inShapeFilter, JPH::TempAllocator& inAllocator)
{
	JPH::Vec3 stickToFloorStepDown = JPH::Vec3(inSettings.mStickToFloorStepDown.x, inSettings.mStickToFloorStepDown.y, inSettings.mStickToFloorStepDown.z);
	JPH::Vec3 walkStairsStepUp = JPH::Vec3(inSettings.mWalkStairsStepUp.x, inSettings.mWalkStairsStepUp.y, inSettings.mWalkStairsStepUp.z);
	JPH::Vec3 walkStairsStepDownExtra = JPH::Vec3(inSettings.mWalkStairsStepDownExtra.x, inSettings.mWalkStairsStepDownExtra.y, inSettings.mWalkStairsStepDownExtra.z);

	// Update the velocity
	JPH::Vec3 desired_velocity = characterVirtual->GetLinearVelocity();
	characterVirtual->SetLinearVelocity(characterVirtual->CancelVelocityTowardsSteepSlopes(desired_velocity));

	// Remember old position
	JPH::RVec3 old_position = characterVirtual->GetPosition();

	// Track if on ground before the update
	bool ground_to_air = characterVirtual->IsSupported();

	// Update the character position (instant, do not have to wait for physics update)
	characterVirtual->Update(deltaTime, inGravity, inBroadPhaseLayerFilter, inObjectLayerFilter, inBodyFilter, inShapeFilter, inAllocator);

	// ... and that we got into air after
	if (characterVirtual->IsSupported())
		ground_to_air = false;

	// If stick to floor enabled and we're going from supported to not supported
	if (ground_to_air && !stickToFloorStepDown.IsNearZero())
	{
		// If we're not moving up, stick to the floor
		float velocity = JPH::Vec3(characterVirtual->GetPosition() - old_position).Dot(characterVirtual->GetUp()) / deltaTime;
		if (velocity <= 1.0e-6f)
			characterVirtual->StickToFloor(stickToFloorStepDown, inBroadPhaseLayerFilter, inObjectLayerFilter, inBodyFilter, inShapeFilter, inAllocator);
	}

	// If walk stairs enabled
	if (!walkStairsStepUp.IsNearZero())
	{
		// Calculate how much we wanted to move horizontally
		JPH::Vec3 desired_horizontal_step = desired_velocity * deltaTime;
		desired_horizontal_step -= desired_horizontal_step.Dot(characterVirtual->GetUp()) * characterVirtual->GetUp();
		float desired_horizontal_step_len = desired_horizontal_step.Length();
		if (desired_horizontal_step_len > 0.0f)
		{
			// Calculate how much we moved horizontally
			JPH::Vec3 achieved_horizontal_step = JPH::Vec3(characterVirtual->GetPosition() - old_position);
			achieved_horizontal_step -= achieved_horizontal_step.Dot(characterVirtual->GetUp()) * characterVirtual->GetUp();

			// Only count movement in the direction of the desired movement
			// (otherwise we find it ok if we're sliding downhill while we're trying to climb uphill)
			JPH::Vec3 step_forward_normalized = desired_horizontal_step / desired_horizontal_step_len;
			achieved_horizontal_step = JPH::max(0.0f, achieved_horizontal_step.Dot(step_forward_normalized)) * step_forward_normalized;
			float achieved_horizontal_step_len = achieved_horizontal_step.Length();

			// If we didn't move as far as we wanted and we're against a slope that's too steep
			if (achieved_horizontal_step_len + 1.0e-4f < desired_horizontal_step_len
				&& characterVirtual->CanWalkStairs(desired_velocity))
			{
				// Calculate how much we should step forward
				// Note that we clamp the step forward to a minimum distance. This is done because at very high frame rates the delta time
				// may be very small, causing a very small step forward. If the step becomes small enough, we may not move far enough
				// horizontally to actually end up at the top of the step.
				JPH::Vec3 step_forward = step_forward_normalized * JPH::max(inSettings.mWalkStairsMinStepForward, desired_horizontal_step_len - achieved_horizontal_step_len);

				// Calculate how far to scan ahead for a floor. This is only used in case the floor normal at step_forward is too steep.
				// In that case an additional check will be performed at this distance to check if that normal is not too steep.
				// Start with the ground normal in the horizontal plane and normalizing it
				JPH::Vec3 step_forward_test = -characterVirtual->GetGroundNormal();
				step_forward_test -= step_forward_test.Dot(characterVirtual->GetUp()) * characterVirtual->GetUp();
				step_forward_test = step_forward_test.NormalizedOr(step_forward_normalized);

				// If this normalized vector and the character forward vector is bigger than a preset angle, we use the character forward vector instead of the ground normal
				// to do our forward test
				if (step_forward_test.Dot(step_forward_normalized) < inSettings.mWalkStairsCosAngleForwardContact)
					step_forward_test = step_forward_normalized;

				// Calculate the correct magnitude for the test vector
				step_forward_test *= inSettings.mWalkStairsStepForwardTest;

				characterVirtual->WalkStairs(deltaTime, walkStairsStepUp, step_forward, step_forward_test, walkStairsStepDownExtra, inBroadPhaseLayerFilter, inObjectLayerFilter, inBodyFilter, inShapeFilter, inAllocator);
			}
		}
	}
}
