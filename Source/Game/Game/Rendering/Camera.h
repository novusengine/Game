#pragma once
#include <Base/Types.h>

// World space is the absolute space in the world
// View space is a space in relation to the camera, the cameras position and direction is 0.
// Clip space is what we get after applying the cameras projection, it could have perspective or be orthographic	xyz: [-1 ... 1] [-1 ... 1] [0 ... 1]
// Screen space is what we get after clip.xyz/clip.w, Z is depth													xyz: [-1 ... 1] [1 ... -1] [near ... far] 
// Viewport space is Screen * Resolution																			xy: [0 ... XResolution] [0 ... YResolution]

struct Camera
{
public:
	mat4x4 clipToView;
	mat4x4 clipToWorld;

	mat4x4 viewToClip;
	mat4x4 viewToWorld;

	mat4x4 worldToView;
	mat4x4 worldToClip;

	vec4 eyePosition;
	vec4 eyeRotation;

	vec4 nearFar;

	vec4 frustum[6] = { vec4(0.0f), vec4(0.0f), vec4(0.0f), vec4(0.0f), vec4(0.0f), vec4(0.0f) };
};

namespace CameraUtils
{
	vec3 ClipToScreen(const Camera& camera, const vec4& clip);
	vec3 ViewToScreen(const Camera& camera, const vec3& view);
	vec3 WorldToScreen(const Camera& camera, const vec3& world);
	
	vec2 ClipToViewport(const Camera& camera, const vec4& clip, const vec2& resolution);
	vec2 ViewToViewport(const Camera& camera, const vec3& view, const vec2& resolution);
	vec2 WorldToViewport(const Camera& camera, const vec3& world, const vec2& resolution);
};