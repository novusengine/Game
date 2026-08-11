#ifndef MODEL_GEOMETRY_INCLUDED
#define MODEL_GEOMETRY_INCLUDED

struct ModelInstanceRecord
{
    float4x4 currentWorld;
    float4x4 previousWorld;
    uint modelIndex;
    uint materialTableOffset;
    uint materialTableCount;
    uint geometryGroupWordOffset;
    uint geometryGroupWordCount;
    uint meshletHistoryWordOffset;
    uint meshletHistoryWordCount;
    uint deformationHandle;
    uint generation;
    uint flags;
    uint packedHighlightColor;
    float highlightIntensity;
};

struct ModelMeshlet
{
    float3 boundsCenter;
    float boundsRadius;
    uint vertexOffset;
    uint triangleOffset;
    uint packedNormalCone;
    uint packedCounts;
};

uint ModelMeshletVertexCount(ModelMeshlet meshlet)
{
    return meshlet.packedCounts & 0xFFFFu;
}

uint ModelMeshletTriangleCount(ModelMeshlet meshlet)
{
    return meshlet.packedCounts >> 16u;
}

uint3 DecodeModelMeshletTriangle(uint packedTriangle)
{
    return uint3(packedTriangle & 0xFFu, (packedTriangle >> 8u) & 0xFFu,
                 (packedTriangle >> 16u) & 0xFFu);
}

float3 DecodePosition(uint2 packedPosition, float3 decodeOffset, float3 decodeExtent)
{
    uint3 quantized = uint3(packedPosition.x & 0xFFFFu, packedPosition.x >> 16u,
                            packedPosition.y & 0xFFFFu);
    return decodeOffset + (float3(quantized) / 65535.0f) * decodeExtent;
}

float3 DecodeOctahedralSnorm(uint packedNormal)
{
    int2 encoded = int2(int(packedNormal << 16u) >> 16u, int(packedNormal) >> 16u);
    float2 octahedral = clamp(float2(encoded) / 32767.0f, -1.0f, 1.0f);
    float3 normal = float3(octahedral, 1.0f - abs(octahedral.x) - abs(octahedral.y));
    float fold = saturate(-normal.z);
    normal.xy += select(normal.xy >= 0.0f, -fold, fold);
    return normalize(normal);
}

float2 DecodeHalf2(uint packedUV)
{
    return float2(f16tof32(packedUV & 0xFFFFu), f16tof32(packedUV >> 16u));
}

int SignExtendByte(uint value, uint shift)
{
    return int(value << (24u - shift)) >> 24u;
}

float4 DecodeNormalCone(uint packedCone)
{
    float3 axis = float3(SignExtendByte(packedCone, 0u), SignExtendByte(packedCone, 8u),
                         SignExtendByte(packedCone, 16u)) / 127.0f;
    float cutoff = float(SignExtendByte(packedCone, 24u)) / 127.0f;
    return float4(normalize(axis), cutoff);
}

float3 SeedColor(uint seed)
{
    return 0.25f + 0.75f * float3(seed & 0xFFu, (seed >> 8u) & 0xFFu, (seed >> 16u) & 0xFFu) / 255.0f;
}

#endif // MODEL_GEOMETRY_INCLUDED
