#ifndef PYRAMID_CULLING_INCLUDED
#define PYRAMID_CULLING_INCLUDED

static float3 axis[8] =
{
    float3(0,0,0),
    float3(1,0,0),
    float3(0,1,0),
    float3(1,1,0),

    float3(0,0,1),
    float3(1,0,1),
    float3(0,1,1),
    float3(1,1,1),
};

float3 TransformToClip(float3 worldPos, float4x4 viewMat)
{
    float4 worldPoint = float4(worldPos.x, worldPos.y, worldPos.z, 1.0);
    float4 clipPoint = mul(worldPoint, viewMat);
    clipPoint /= clipPoint.w;

    return clipPoint.xyz;
}

bool IsVisible(float3 AABBMin, float3 AABBMax, float3 eye, Texture2D<float> pyramid, SamplerState samplerState, float4x4 viewMat, uint2 viewportSize)
{
    if (eye.x < AABBMax.x && eye.x > AABBMin.x)
    {
        if (eye.y < AABBMax.y && eye.y > AABBMin.y)
        {
            if (eye.z < AABBMax.z && eye.z > AABBMin.z)
            {
                return true;
            }
        }
    }

    float3 center = TransformToClip(lerp(AABBMin, AABBMax, 0.5), viewMat);
    center.xy = clamp(center.xy, float2(-1.0f, -1.0f), float2(1.0f, 1.0f));

    float2 pmin = center.xy;
    float2 pmax = center.xy;
    // x max, y min
    float2 depth = center.z;

    for (int i = 0; i < 8; i++)
    {
        float pX = lerp(AABBMin.x, AABBMax.x, axis[i].x);
        float pY = lerp(AABBMin.y, AABBMax.y, axis[i].y);
        float pZ = lerp(AABBMin.z, AABBMax.z, axis[i].z);

        float3 clipPoint = TransformToClip(float3(pX, pY, pZ), viewMat);
        clipPoint.xy = clamp(clipPoint.xy, float2(-1.0f, -1.0f), float2(1.0f, 1.0f));

        pmin.x = min(clipPoint.x, pmin.x);
        pmin.y = min(clipPoint.y, pmin.y);

        pmax.x = max(clipPoint.x, pmax.x);
        pmax.y = max(clipPoint.y, pmax.y);

        depth.x = max(clipPoint.z, depth.x);
        depth.y = min(clipPoint.z, depth.y);
    }

    //if (pmin.x == pmax.x || pmin.y == pmax.y)
    //{
    //    return false;
    //}

    uint pyrWidth;
    uint pyrHeight;
    pyramid.GetDimensions(pyrWidth, pyrHeight);

    // convert max and min into UV space
    pmin = pmin * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    pmax = pmax * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

    // calculate pixel widths/height
    float boxWidth = abs(pmax.x - pmin.x) * (float)pyrWidth;
    float boxHeight = abs(pmax.y - pmin.y) * (float)pyrHeight;

    float level = ceil(log2(max(boxWidth, boxHeight)));

    float2 psample = lerp(pmin, pmax, 0.5);

    float sampleDepth = pyramid.SampleLevel(samplerState, psample, level).x;

    return sampleDepth <= depth.x;
};

// We don't want to run occlusion culling if the object intersects the near plane, thanks Eichenherz!
bool IsIntersectingNearZ(float3 aabbMin, float3 aabbMax, float4x4 m)
{
    float3 aabbSize = aabbMax - aabbMin;
    float4 clipCorners[] = {
        mul(float4(aabbMin, 1), m),
        mul(float4(aabbMin + float3(aabbSize.x, 0, 0), 1), m),
        mul(float4(aabbMin + float3(0, aabbSize.y, 0), 1), m),
        mul(float4(aabbMin + float3(0, 0, aabbSize.z), 1), m),
        mul(float4(aabbMin + float3(aabbSize.xy, 0), 1), m),
        mul(float4(aabbMin + float3(0, aabbSize.yz), 1), m),
        mul(float4(aabbMin + float3(aabbSize.x, 0, aabbSize.z), 1), m),
        mul(float4(aabbMin + aabbSize, 1), m)
    };

    float minW = min(
        min(min(clipCorners[0].w, clipCorners[1].w), min(clipCorners[2].w, clipCorners[3].w)),
        min(min(clipCorners[4].w, clipCorners[5].w), min(clipCorners[6].w, clipCorners[7].w)));

    return minW <= 0.0f;
}

#endif // PYRAMID_CULLING_INCLUDED