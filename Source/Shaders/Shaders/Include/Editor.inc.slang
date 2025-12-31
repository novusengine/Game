#ifndef EDITOR_INCLUDED
#define EDITOR_INCLUDED

#include "Include/VisibilityBuffers.inc.slang"

float sdCapsule(float3 p, float3 a, float3 b)
{
    float3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

float sdCapsule(float2 p, float2 a, float2 b)
{
    float2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// 1.0f means draw wireframe, 0.0f means don't
float WireframeTriangle(float3 pos, float3 v0, float3 v1, float3 v2)
{
    float distanceToEdge0 = sdCapsule(pos, v0, v1);
    float distanceToEdge1 = sdCapsule(pos, v1, v2);
    float distanceToEdge2 = sdCapsule(pos, v0, v2);

    float minDistanceToEdge = min(min(distanceToEdge0, distanceToEdge1), distanceToEdge2);

    if (minDistanceToEdge < 0.001f)
    {
        return 1.0f;
    }

    return 0.0f;
}

// 1.0f means draw wireframe, 0.0f means don't
float WireframeEdge(float2 pos, float2 v0, float2 v1)
{
    // TODO: Anti-aliasing by taking several samples with a jittered position
    const float testDistance = 0.001f;

    float distanceToEdge = sdCapsule(pos, v0, v1);
    if (distanceToEdge < testDistance)
    {
        return 1.0f;
    }

    return 0.0f;
}

// Highlights terrain vertices inside the circle brush
float WireframeTriangleCorners(float3 pixelPos, float3 v0, float3 v1, float3 v2)
{
    float distanceToCorner0 = distance(pixelPos, v0);
    float distanceToCorner1 = distance(pixelPos, v1);
    float distanceToCorner2 = distance(pixelPos, v2);

    float minDistanceToCorner = min(min(distanceToCorner0, distanceToCorner1), distanceToCorner2);

    if (minDistanceToCorner < 0.004f)
    {
        return 1.0f;
    }

    return 0.0f;
}

float sdSphere(float3 p, float s)
{
    return length(p) - s;
}

// 0.0f means draw the brush, 1.0f means don't modify the pixel
float EditorCircleBrush(float3 mousePos, float3 pixelPos, float radius, float falloff, Barycentrics bary)
{
    float distanceFromCenter = length(mousePos.xz - pixelPos.xz);
    float smoothStepEdge = radius - falloff;
    if (distanceFromCenter < smoothStepEdge)
        return 1.0f; // Inside the circle without falloff, draw the brush
    else if (distanceFromCenter < radius)
        return smoothstep(radius, smoothStepEdge, distanceFromCenter); // Falloff region
    else
        return 1.0f; // Outside the circle, do not modify the pixel
}
#endif // EDITOR_INCLUDED