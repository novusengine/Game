#ifndef LIGHT_SHARED_INCLUDED
#define LIGHT_SHARED_INCLUDED
#include "Include/Common.inc.slang"

static const uint TILED_CULLING_BLOCKSIZE = 16;
static const uint TILED_CULLING_THREADSIZE = 8;
static const uint TILED_CULLING_GRANULARITY = TILED_CULLING_BLOCKSIZE / TILED_CULLING_THREADSIZE;

struct PackedDecal
{
    float4 positionAndTextureID; // xyz = position, w = texture index
    float4 rotationQuat;
    float4 extentsAndColor; // xyz = extents, asuint(w) = uint color multiplier
    float4 thresholds; // x = f16 min/max threshold, y = f16 min UV, z = f16 max UV, asuint(w) = flags
    float4 composition; // x = opacity, asint(y) = priority, asuint(z) = stable ID
};

enum DecalFlags
{
    DECAL_FLAG_TWOSIDED = 1 << 0,
};

struct Decal
{
    float3 position;
    float4 rotationQuat;
    float3 extents;
    float3 color;
    float minThreshold;
    float maxThreshold;
    float2 minUV;
    float2 maxUV;
    uint textureID;
    uint flags;
    float opacity;
    int priority;
    uint stableID;

    bool IsTwoSided() { return (flags & DecalFlags::DECAL_FLAG_TWOSIDED) != 0; }
};

Decal UnpackDecal(PackedDecal pd)
{
    Decal d;
    d.position = pd.positionAndTextureID.xyz;
    d.textureID = asuint(pd.positionAndTextureID.w);

    d.rotationQuat = pd.rotationQuat;
    d.extents = pd.extentsAndColor.xyz;

    uint colorUint = asuint(pd.extentsAndColor.w); // ABGR
    d.color = float3(((colorUint >> 24) & 0xFF) / 255.0f, ((colorUint >> 16) & 0xFF) / 255.0f, ((colorUint >> 8) & 0xFF) / 255.0f);

    // Unpack minThreshold and maxThreshold from 2xf16 packed into a f32
    d.minThreshold = f16tof32(asuint(pd.thresholds.x) & 0xFFFF);
    d.maxThreshold = f16tof32((asuint(pd.thresholds.x) >> 16) & 0xFFFF);

    // Unpack minUV and maxUV from 4xf16 packed into 2xf32
    d.minUV = float2(f16tof32(asuint(pd.thresholds.y) & 0xFFFF), f16tof32((asuint(pd.thresholds.y) >> 16) & 0xFFFF));
    d.maxUV = float2(f16tof32(asuint(pd.thresholds.z) & 0xFFFF), f16tof32((asuint(pd.thresholds.z) >> 16) & 0xFFFF));

    d.flags = asuint(pd.thresholds.w);
    d.opacity = pd.composition.x;
    d.priority = asint(pd.composition.y);
    d.stableID = asuint(pd.composition.z);

    return d;
}

#endif // LIGHT_SHARED_INCLUDED
