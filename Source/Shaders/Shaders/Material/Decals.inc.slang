#ifndef MATERIAL_DECALS_INCLUDED
#define MATERIAL_DECALS_INCLUDED

#include "DescriptorSet/Light.inc.slang"
#include "DescriptorSet/Material.inc.slang"
#include "Material/MaterialContract.inc.slang"

static const uint DECAL_TILE_LIST_CAPACITY = 8u;

static const uint DECAL_SURFACE_BASE_COLOR = 1u << 0u;
static const uint DECAL_SURFACE_NORMAL = 1u << 1u;
static const uint DECAL_SURFACE_ROUGHNESS = 1u << 2u;
static const uint DECAL_SURFACE_METALNESS = 1u << 3u;
static const uint DECAL_SURFACE_EMISSIVE = 1u << 4u;
static const uint DECAL_BLEND_REPLACE = 0u;
static const uint DECAL_BLEND_ADD = 1u;

struct DecalSurface
{
    float3 baseColor;
    float3 normal;
    float roughness;
    float metalness;
    float3 emissive;
    float coverage;
    uint channelWriteMask;
    uint packedBlendModes;
    float4 channelStrengths0;
    float channelStrengthEmissive;
};

uint DecalChannelBlendMode(DecalSurface decalSurface, uint channel)
{
    return (decalSurface.packedBlendModes >> (channel * 2u)) & 3u;
}

float3 BlendDecalValue(float3 receiver, float3 decal, float amount, uint blendMode)
{
    if (blendMode == DECAL_BLEND_ADD)
        return receiver + decal * amount;
    return lerp(receiver, decal, amount);
}

float BlendDecalValue(float receiver, float decal, float amount, uint blendMode)
{
    if (blendMode == DECAL_BLEND_ADD)
        return receiver + decal * amount;
    return lerp(receiver, decal, amount);
}

void ApplyDecalSurface(inout SurfaceDescription surface, DecalSurface decalSurface, float opacity)
{
    float effectiveOpacity = saturate(decalSurface.coverage * opacity);
    if (effectiveOpacity <= 0.0f)
        return;

    if ((decalSurface.channelWriteMask & DECAL_SURFACE_BASE_COLOR) != 0u)
        surface.albedo = BlendDecalValue(surface.albedo, decalSurface.baseColor, effectiveOpacity * decalSurface.channelStrengths0.x, DecalChannelBlendMode(decalSurface, 0u));
    if ((decalSurface.channelWriteMask & DECAL_SURFACE_NORMAL) != 0u)
    {
        float3 normal = normalize(lerp(UnpackSurfaceNormal(surface.packedNormal), decalSurface.normal, effectiveOpacity * decalSurface.channelStrengths0.y));
        surface.packedNormal = PackSurfaceNormal(normal);
    }
    if ((decalSurface.channelWriteMask & DECAL_SURFACE_ROUGHNESS) != 0u)
        surface.roughness = BlendDecalValue(surface.roughness, decalSurface.roughness, effectiveOpacity * decalSurface.channelStrengths0.z, DecalChannelBlendMode(decalSurface, 2u));
    if ((decalSurface.channelWriteMask & DECAL_SURFACE_METALNESS) != 0u)
        surface.metalness = BlendDecalValue(surface.metalness, decalSurface.metalness, effectiveOpacity * decalSurface.channelStrengths0.w, DecalChannelBlendMode(decalSurface, 3u));
    if ((decalSurface.channelWriteMask & DECAL_SURFACE_EMISSIVE) != 0u)
    {
        float3 receiverEmissive = surface.emissive * surface.emissiveIntensity;
        surface.emissive = BlendDecalValue(receiverEmissive, decalSurface.emissive, effectiveOpacity * decalSurface.channelStrengthEmissive, DecalChannelBlendMode(decalSurface, 4u));
        surface.emissiveIntensity = 1.0f;
    }
}

bool EvaluateProjectedDecal(float3 pixelWorldPosition, float3 pixelWorldNormal, Decal decal, out DecalSurface decalSurface)
{
    decalSurface = {};
    float4 inverseRotation = float4(-decal.rotationQuat.xyz, decal.rotationQuat.w);
    float3 pixelLocal = mul(pixelWorldPosition - decal.position, QuatToMat(inverseRotation));
    float2 planar = pixelLocal.xy;
    if (abs(pixelLocal.z) > decal.extents.z || any(abs(planar) > decal.extents.xy))
        return false;

    if (!decal.IsTwoSided())
    {
        float3 projectionDirection = normalize(mul(float3(0.0f, 0.0f, -1.0f), QuatToMat(decal.rotationQuat)));
        float facing = dot(normalize(pixelWorldNormal), projectionDirection);
        if (facing < decal.minThreshold || facing > decal.maxThreshold)
            return false;
    }

    float2 uv01 = planar * (0.5f / decal.extents.xy) + 0.5f;
    float2 uvSpan = decal.maxUV - decal.minUV;
    float2 uv = decal.minUV + uv01 * uvSpan;
    float2 uvDDX;
    float2 uvDDY;
    ComputeGradients_Quad(uv01, uvDDX, uvDDY);
    Texture2D<float4> texture = _materialTextures[NonUniformResourceIndex(decal.textureID)];
    float4 sample = texture.SampleGrad(_materialSampler0, uv, uvDDX * uvSpan, uvDDY * uvSpan);

    decalSurface.emissive = sample.rgb * decal.color;
    decalSurface.coverage = sample.a;
    decalSurface.channelWriteMask = DECAL_SURFACE_EMISSIVE;
    decalSurface.packedBlendModes = DECAL_BLEND_ADD << (4u * 2u);
    decalSurface.channelStrengths0 = 1.0f;
    decalSurface.channelStrengthEmissive = 1.0f;
    return true;
}

void ApplyDecals(uint tileIndex, float3 pixelWorldPosition, float3 pixelWorldNormal, inout SurfaceDescription surface)
{
    uint tileAddress = 1u + tileIndex * (1u + DECAL_TILE_LIST_CAPACITY);
    uint decalCount = min(_entityTiles[tileAddress], DECAL_TILE_LIST_CAPACITY);
    for (uint index = 0u; index < decalCount; ++index)
    {
        Decal decal = UnpackDecal(_packedDecals[_entityTiles[tileAddress + 1u + index]]);
        DecalSurface decalSurface;
        if (EvaluateProjectedDecal(pixelWorldPosition, pixelWorldNormal, decal, decalSurface))
            ApplyDecalSurface(surface, decalSurface, decal.opacity);
    }
}

#endif
