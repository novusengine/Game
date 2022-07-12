#include "Camera.h"

vec3 CameraUtils::ClipToScreen(const Camera& camera, const vec4& clip)
{
	return vec3(clip) / clip.w;
}

vec3 CameraUtils::ViewToScreen(const Camera& camera, const vec3& view)
{
	vec4 clip = vec4(view, 1.0f) * camera.viewToClip;
	return vec3(clip) / clip.w;
}

vec3 CameraUtils::WorldToScreen(const Camera& camera, const vec3& world)
{
	vec4 clip = vec4(world, 1.0f) * camera.worldToClip;
	return vec3(clip) / clip.w;
}

vec2 CameraUtils::ClipToViewport(const Camera& camera, const vec4& clip, const vec2& resolution)
{
	vec3 screen = vec3(clip) / clip.w;
	return vec2(screen) * resolution;
}

vec2 CameraUtils::ViewToViewport(const Camera& camera, const vec3& view, const vec2& resolution)
{
	vec4 clip = vec4(view, 1.0f) * camera.viewToClip;
	vec3 screen = vec3(clip) / clip.w;
	return vec2(screen) * resolution;
}

vec2 CameraUtils::WorldToViewport(const Camera& camera, const vec3& world, const vec2& resolution)
{
	vec4 clip = vec4(world, 1.0f) * camera.worldToClip;
	vec3 screen = vec3(clip) / clip.w;
	return vec2(screen) * resolution;
}
