#ifndef OIT_INC_INCLUDED
#define OIT_INC_INCLUDED
#include "common.inc.hlsl"

#define MODE 0

float CalculateOITWeight(float4 color, float z)
{
    float a = max(min(1.0f, max(max(color.r, color.g), color.b) * color.a), color.a);
    a *= a;
    float b = -z * 0.95 + 1.0;

    // If your scene has a lot of content very close to the far plane,
    //   then include this line (one rsqrt instruction):
    //   b /= sqrt(1e4 * abs(csZ));
    float weight = clamp(a * a * a * 1e8 * b * b * b, 1e-2, 3e2);

    return weight;
}

#endif