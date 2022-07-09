#ifndef VIEWDATA_INCLUDED
#define VIEWDATA_INCLUDED

struct ViewData
{
    float4x4 viewProjectionMatrix;
    float4x4 invViewProjectionMatrix;
    float4x4 viewMatrix;
    float4 eyePositionXYZAndSplitDepth; // W component holds split depth if it's a cascade
    float4 eyeRotation;
    float4 frustum[6];
};

#endif // VIEWDATA_INCLUDED