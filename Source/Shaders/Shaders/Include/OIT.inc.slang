#ifndef OIT_INC_INCLUDED
#define OIT_INC_INCLUDED

#include "Include/Common.inc.slang"

#define MODE 0

float CalculateOITWeight(float4 color, float clipSpaceZ, float viewSpaceZ)
{
    float a = min(1.0f, color.a);
    a *= a;
    float b = -clipSpaceZ * 0.95 + 1.0;
    b /= sqrt(1e4 * abs(viewSpaceZ));

    float weight = clamp(a * a * a * 1e8 * b * b * b, 1e-2, 3e2);

    return weight;
}

#endif