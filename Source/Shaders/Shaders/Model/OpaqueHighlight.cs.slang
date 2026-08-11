#include "Include/Common.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelGeometry.inc.slang"
#include "Model/ModelViewWorkStats.inc.slang"
#include "Model/ModelVisibilityRecord.inc.slang"

struct Constants
{
    uint2 renderSize;
    uint resourceIndex;
    uint padding;
};

[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_PASS)]] Texture2D<uint> _highlightVisibility;
[[vk::binding(1, PER_PASS)]] RWTexture2D<float4> _highlightColor;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _highlightModelVisibilityRecords0;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<uint> _highlightModelVisibilityStats0;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _highlightModelVisibilityRecords1;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<uint> _highlightModelVisibilityStats1;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _highlightModelInstances;

bool LoadHighlightInstance(uint2 pixel, out uint instanceIndex, out uint generation,
                           out float intensity, out uint packedColor)
{
    uint visibility = _highlightVisibility[pixel];
    if (!IsModelVisibility(visibility))
        return false;

    TypedVisibilityPayload payload = UnpackTypedVisibility(visibility);
    uint recordCount = _constants.resourceIndex == 0u
        ? _highlightModelVisibilityStats0[MODEL_WORK_STAT_VISIBILITY_RECORDS]
        : _highlightModelVisibilityStats1[MODEL_WORK_STAT_VISIBILITY_RECORDS];
    if (payload.recordID >= recordCount)
        return false;

    ModelVisibilityRecord packedRecord = _constants.resourceIndex == 0u
        ? _highlightModelVisibilityRecords0[payload.recordID]
        : _highlightModelVisibilityRecords1[payload.recordID];
    instanceIndex = DecodeModelVisibilityRecord(packedRecord).instanceIndex;
    ModelInstanceRecord instance = _highlightModelInstances[instanceIndex];
    generation = instance.generation;
    intensity = instance.highlightIntensity;
    packedColor = instance.packedHighlightColor;
    return true;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    if (any(pixel >= _constants.renderSize))
        return;

    uint currentIndex = 0u;
    uint currentGeneration = 0u;
    float currentIntensity = 1.0f;
    uint currentPackedColor = 0xFFFFFFFFu;
    bool currentIsModel = LoadHighlightInstance(pixel, currentIndex, currentGeneration,
                                                currentIntensity, currentPackedColor);
    float4 color = _highlightColor[pixel];
    if (currentIsModel && currentIntensity != 1.0f)
    {
        color.rgb *= currentIntensity;
        _highlightColor[pixel] = color;
        return;
    }

    static const int2 NEIGHBOR_OFFSETS[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
    for (uint neighbor = 0u; neighbor < 4u; ++neighbor)
    {
        int2 neighborPixel = int2(pixel) + NEIGHBOR_OFFSETS[neighbor];
        if (any(neighborPixel < 0) || any(neighborPixel >= int2(_constants.renderSize)))
            continue;

        uint neighborIndex = 0u;
        uint neighborGeneration = 0u;
        float neighborIntensity = 1.0f;
        uint neighborPackedColor = 0xFFFFFFFFu;
        if (!LoadHighlightInstance(uint2(neighborPixel), neighborIndex, neighborGeneration,
                                   neighborIntensity, neighborPackedColor) || neighborIntensity == 1.0f)
            continue;
        if (currentIsModel && currentIndex == neighborIndex && currentGeneration == neighborGeneration)
            continue;

        float3 highlightColor = PackedUnormsToFloat4(neighborPackedColor).rgb;
        color.rgb += highlightColor * max(neighborIntensity - 1.0f, 0.0f);
        _highlightColor[pixel] = color;
        return;
    }
}
