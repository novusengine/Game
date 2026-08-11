#ifndef MATERIAL_CONTRACT_INCLUDED
#define MATERIAL_CONTRACT_INCLUDED

#include "Include/Common.inc.slang"
#include "Material/MaterialABI.inc.slang"

#define MATERIAL_ALPHA_CUTOFF_OFFSET 52u

// Inputs available to every authored EvaluateMaterial and EvaluateCoverage function.
struct MaterialInput
{
    float3 worldPosition;
    float3 geometricNormal;
    float4 vertexColor;
    float2 uv0;
    float2 uv0DDX;
    float2 uv0DDY;
    bool frontFacing;
};

// Stable output of authored Materials. Lighting models consume this without source-specific data.
struct SurfaceDescription
{
    float3 albedo;
    float alpha;
    uint packedNormal;
    float roughness;
    float metalness;
    uint flags;
    float3 emissive;
    float emissiveIntensity;
    uint customData0;
    uint customData1;
};

// Stable renderer input to authored LightingModel functions.
struct LightingModelInput
{
    float3 worldPosition;
    float3 viewDirection;
    float3 lightDirection;
    float3 ambientColor;
    float3 lightColor;
    float3 shadowColor;
    float ambientVisibility;
    float shadowVisibility;
    float lightStrength;
};

uint PackSurfaceNormal(float3 normal)
{
    float2 oct = OctNormalEncode(normalize(normal));
    return f32tof16(oct.x) | (f32tof16(oct.y) << 16u);
}

float3 UnpackSurfaceNormal(uint packedNormal)
{
    return OctNormalDecode(float2(f16tof32(packedNormal), f16tof32(packedNormal >> 16u)));
}

SurfaceDescription MakeDefaultSurface(MaterialInput input)
{
    SurfaceDescription surface;
    surface.albedo = 1.0f;
    surface.alpha = 1.0f;
    surface.packedNormal = PackSurfaceNormal(input.geometricNormal);
    surface.roughness = 1.0f;
    surface.metalness = 0.0f;
    surface.flags = 0u;
    surface.emissive = 0.0f;
    surface.emissiveIntensity = 0.0f;
    surface.customData0 = 0u;
    surface.customData1 = 0u;
    return surface;
}

#endif
