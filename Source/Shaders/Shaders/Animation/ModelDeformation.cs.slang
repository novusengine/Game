#include "Model/ModelGeometry.inc.slang"

struct AffineFloat3x4
{
    float4 row0;
    float4 row1;
    float4 row2;
};

struct ModelDeformationWork
{
    float4 positionDecodeOffset;
    float4 positionDecodeExtent;
    uint sourcePositionOffset;
    uint sourceVertexAttributeOffset;
    uint sourceSkinningOffset;
    uint vertexCount;
    uint paletteOffset;
    uint paletteCount;
    uint outputPositionOffset;
    uint outputVertexAttributeOffset;
};

struct Constants
{
    uint workCount;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelDeformationWork> _animationSkinningWork;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<AffineFloat3x4> _animationSkinningPalettes;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint2> _animationSourcePositions;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<uint4> _animationSourceVertexAttributes;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<uint2> _animationSourceSkinning;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<float4> _animationDeformedPositions;
[[vk::binding(6, PER_PASS)]] RWStructuredBuffer<uint4> _animationDeformedVertexAttributes;

float3 TransformPoint(AffineFloat3x4 transform, float3 point)
{
    float4 value = float4(point, 1.0f);
    return float3(dot(transform.row0, value), dot(transform.row1, value), dot(transform.row2, value));
}

float3 TransformVector(AffineFloat3x4 transform, float3 vector)
{
    float4 value = float4(vector, 0.0f);
    return float3(dot(transform.row0, value), dot(transform.row1, value), dot(transform.row2, value));
}

float2 EncodeOctahedral(float3 value)
{
    value /= max(abs(value.x) + abs(value.y) + abs(value.z), 1.0e-8f);
    float2 encoded = value.xy;
    if (value.z < 0.0f)
        encoded = (1.0f - abs(encoded.yx)) * select(encoded >= 0.0f, 1.0f, -1.0f);
    return clamp(encoded, -1.0f, 1.0f);
}

uint PackOctahedralSnorm16(float3 value)
{
    int2 encoded = int2(round(EncodeOctahedral(normalize(value)) * 32767.0f));
    return (uint(encoded.x) & 0xFFFFu) | ((uint(encoded.y) & 0xFFFFu) << 16u);
}

float4 DecodeTangent(uint packed)
{
    int x = int(packed << 17u) >> 17u;
    int y = int(packed << 2u) >> 17u;
    float2 octahedral = clamp(float2(x, y) / 16383.0f, -1.0f, 1.0f);
    float3 tangent = float3(octahedral, 1.0f - abs(octahedral.x) - abs(octahedral.y));
    float fold = saturate(-tangent.z);
    tangent.xy += select(tangent.xy >= 0.0f, -fold, fold);
    return float4(normalize(tangent), (packed & (1u << 30u)) != 0u ? -1.0f : 1.0f);
}

uint PackTangent(float3 value, float handedness)
{
    int2 encoded = int2(round(EncodeOctahedral(normalize(value)) * 16383.0f));
    uint packed = (uint(encoded.x) & 0x7FFFu) | ((uint(encoded.y) & 0x7FFFu) << 15u);
    return packed | (handedness < 0.0f ? (1u << 30u) : 0u);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID)
{
    if (groupID.x >= _constants.workCount)
        return;
    ModelDeformationWork work = _animationSkinningWork[groupID.x];
    for (uint vertex = threadID.x; vertex < work.vertexCount; vertex += 64u)
    {
        float3 sourcePosition = DecodePosition(_animationSourcePositions[work.sourcePositionOffset + vertex], work.positionDecodeOffset.xyz, work.positionDecodeExtent.xyz);
        uint4 sourceAttributes = _animationSourceVertexAttributes[work.sourceVertexAttributeOffset + vertex];
        float3 sourceNormal = DecodeOctahedralSnorm(sourceAttributes.x);
        float4 sourceTangent = DecodeTangent(sourceAttributes.y);
        uint2 skinning = _animationSourceSkinning[work.sourceSkinningOffset + vertex];

        float3 position = 0.0f;
        float3 normal = 0.0f;
        float3 tangent = 0.0f;
        float weightSum = 0.0f;
        for (uint influence = 0u; influence < 4u; ++influence)
        {
            uint joint = (skinning.x >> (influence * 8u)) & 0xFFu;
            float weight = float((skinning.y >> (influence * 8u)) & 0xFFu) / 255.0f;
            if (weight == 0.0f || joint >= work.paletteCount)
                continue;
            AffineFloat3x4 transform = _animationSkinningPalettes[work.paletteOffset + joint];
            position += TransformPoint(transform, sourcePosition) * weight;
            normal += TransformVector(transform, sourceNormal) * weight;
            tangent += TransformVector(transform, sourceTangent.xyz) * weight;
            weightSum += weight;
        }
        if (weightSum <= 0.0f)
        {
            position = sourcePosition;
            normal = sourceNormal;
            tangent = sourceTangent.xyz;
        }
        else
        {
            position /= weightSum;
        }
        uint4 outputAttributes = sourceAttributes;
        outputAttributes.x = PackOctahedralSnorm16(normal);
        outputAttributes.y = PackTangent(tangent, sourceTangent.w);
        _animationDeformedPositions[work.outputPositionOffset + vertex] = float4(position, 1.0f);
        _animationDeformedVertexAttributes[work.outputVertexAttributeOffset + vertex] = outputAttributes;
    }
}
