#ifndef LIGHT_SHARED_INCLUDED
#define LIGHT_SHARED_INCLUDED
#include "Include/Common.inc.slang"

static const uint TILED_CULLING_BLOCKSIZE = 16;
static const uint TILED_CULLING_THREADSIZE = 8;
static const uint TILED_CULLING_GRANULARITY = TILED_CULLING_BLOCKSIZE / TILED_CULLING_THREADSIZE;
static const uint SHADER_ENTITY_TILE_BUCKET_COUNT = 1; // Arbitrary limit, should be enough for most cases, just make sure this is equal or higher than maxDecalsPerTile

struct PackedDecal
{
    float4 positionAndTextureID; // xyz = position, w = texture index
    float4 rotationQuat;
    float4 extentsAndColor; // xyz = extents, asuint(w) = uint color multiplier
    float4 thresholds; // x = f16 min/max threshold, y = f16 min UV, z = f16 max UV, asuint(w) = flags
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

    bool IsTwoSided() { return (flags & DecalFlags::DECAL_FLAG_TWOSIDED) != 0; }
};

float3 ApplyDecal(float3 pixelWS, float3 normalWS, Decal d, Texture2D decalTexture, SamplerState sampler)
{
    const float3 halfExtents = d.extents.xyz;
    const float3 center = d.position.xyz;
    const uint   texId = d.textureID;

    // world -> local (inverse rotation via conjugate)
    const float4   rotQuatInv = float4(-d.rotationQuat.xyz, d.rotationQuat.w);
    const float3x3 rotMatInv = QuatToMat(rotQuatInv);
    const float3   pixelLocal = mul(pixelWS - center, rotMatInv); // local coords

    // Select planar axes and signed depth along projector axis
    float2 planar = pixelLocal.xy;
    float2 extents2D = halfExtents.xy;
    float  depthS = pixelLocal.z;
    float  extentsDepth = halfExtents.z;

    // Full OBB thickness and footprint
    if (depthS < -extentsDepth || depthS > extentsDepth)
        return 0;

    if (any(abs(planar) > extents2D))
        return 0;

    if (!d.IsTwoSided())
    {
        // Facing gate (reject only the underside)
        const float3   localFwd = float3(0, 0, -1);
        const float3x3 rotationMat = QuatToMat(d.rotationQuat);
        const float3   decalProjDirWS = normalize(mul(normalize(localFwd), rotationMat));
        normalWS = normalize(normalWS);

        // If the surface normal points roughly the SAME way as the projector forward, it's the underside -> reject.
        const float minSameDirThreshold = d.minThreshold;
        const float maxSameDirThreshold = d.maxThreshold;

        float pixelDotDecalProjDir = dot(normalWS, decalProjDirWS);
        if (pixelDotDecalProjDir < minSameDirThreshold)
            return 0;
        if (pixelDotDecalProjDir > maxSameDirThreshold)
            return 0;
    }

    // Map planar [-extents2D, +extents2D] -> [0,1]
    const float2 uv01 = planar * (0.5f / extents2D) + 0.5f;

    // Map [0,1] -> [minUV,maxUV]
    const float2 uvMin = d.minUV;
    const float2 uvMax = d.maxUV;
    const float2 uvSpan = uvMax - uvMin;

    // Gradients for compute shader sampling
    float2 ddx01, ddy01;
    ComputeGradients_Quad(uv01, ddx01, ddy01);

    // Scale gradients to keep mip selection correct
    const float2 uv = uvMin + uv01 * uvSpan;
    const float2 ddx = ddx01 * uvSpan;
    const float2 ddy = ddy01 * uvSpan;

    float4 c = decalTexture.SampleGrad(sampler, uv, ddx, ddy);
    c.rgb *= d.color;

    return c.rgb * c.a;
}

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

    return d;
}

#endif // LIGHT_SHARED_INCLUDED