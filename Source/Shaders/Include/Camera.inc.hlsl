#ifndef CAMERA_INCLUDED
#define CAMERA_INCLUDED

struct Camera
{
	float4x4 clipToView;
	float4x4 clipToWorld;

	float4x4 viewToClip;
	float4x4 viewToWorld;

	float4x4 worldToView;
	float4x4 worldToClip;

	float4 eyePosition;
	float4 eyeRotation;

	float4 nearFar;

	float4 frustum[6];
};

#endif // CAMERA_INCLUDED