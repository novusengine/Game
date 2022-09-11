#pragma once

namespace ECS::Components
{
	// World space is the absolute space in the world
	// View space is a space in relation to the camera, the cameras position and direction is 0.
	// Clip space is what we get after applying the cameras projection, it could have perspective or be orthographic	xyz: [-1 ... 1] [-1 ... 1] [0 ... 1]
	// Screen space is what we get after clip.xyz/clip.w, Z is depth													xyz: [-1 ... 1] [1 ... -1] [near ... far] 
	// Viewport space is Screen * Resolution																			xy: [0 ... XResolution] [0 ... YResolution]

	struct Camera
	{
	public:
		bool dirtyView = true; // Do we need to recalculate the matrices?
		f32 pitch = 0.0f;
		f32 yaw = 0.0f;
		f32 roll = 0.0f;

		bool dirtyPerspective = true; // Do we need to recalculate the perspectives?
		f32 fov = 75.0f;
		f32 aspectRatio = 1.0f;
		f32 nearClip = 0.01f;
		f32 farClip = 1000.0f;

		
		mat4x4 clipToView;
		mat4x4 clipToWorld;

		mat4x4 viewToClip;
		mat4x4 viewToWorld;

		mat4x4 worldToView;
		mat4x4 worldToClip;

		u32 cameraBindSlot = 0; // Which camera matrix slot this should bind to
	};
}