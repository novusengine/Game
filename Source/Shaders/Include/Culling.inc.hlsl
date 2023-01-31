#ifndef CULLING_INCLUDED
#define CULLING_INCLUDED

struct AABB
{
    float3 min;
    float3 max;
};

#define MAX_SHADOW_CASCADES 8
#define NUM_CULL_VIEWS 1 + MAX_SHADOW_CASCADES // Main view plus max number of cascades

#endif // CULLING_INCLUDED