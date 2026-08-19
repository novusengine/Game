#include "Shadows/SVSM.inc.slang"

struct Constants
{
    uint queueIndex;
    uint frameIndex;
    uint dynamicCaster;
    uint opacityDither;
    uint queueCapacity;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(31, PER_PASS)]] StructuredBuffer<SVSMData> _shadowRasterSVSMData;
[[vk::binding(32, PER_PASS)]] StructuredBuffer<uint> _shadowRasterPageTable;
[[vk::binding(33, PER_PASS)]] RWTexture2D<uint> _shadowRasterPagePool;

struct PSInput
{
    float4 position : SV_Position;
    nointerpolation uint clipmapIndex : TEXCOORD0;
    nointerpolation float opacity : TEXCOORD1;
};

[shader("fragment")]
void main(PSInput input)
{
    if (_constants.opacityDither != 0u && SVSMOpacityDiscard(input.opacity, input.position.xy, input.clipmapIndex))
        discard;

    uint requiredFlags = _constants.dynamicCaster != 0u ? SVSM_PAGE_RESIDENT :
                                                          SVSM_PAGE_RESIDENT | SVSM_PAGE_DIRTY;
    uint poolPagesPerRow = _constants.dynamicCaster != 0u ?
        _shadowRasterSVSMData[0].configDynamicPoolPagesPerRow :
        _shadowRasterSVSMData[0].configPoolPagesPerRow;
    uint pageEntry = SVSMLoadPageEntry(input.position.xy, input.clipmapIndex, _shadowRasterSVSMData,
                                       _shadowRasterPageTable);
    SVSMWritePageDepthEntry(input.position.xyz, pageEntry, requiredFlags, poolPagesPerRow,
                            _shadowRasterSVSMData, _shadowRasterPagePool);
}
