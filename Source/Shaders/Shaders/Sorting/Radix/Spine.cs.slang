// Reduce-then-scan radix sort -- spine pass.
// For each radix bin, prefix-scans the per-partition histograms so every partition knows its
// starting offset for every bin. Also prefix-scans the per-pass global histogram (bin 0's
// workgroup handles that).
// Dispatched with `(RADIX=256, 1, 1)` groups, WORKGROUP_SIZE threads each.

#include "Sorting/Radix/Constants.inc.slang"

struct RadixPushConstants
{
    uint pass;
    uint elementCount;
};
[[vk::push_constant]] RadixPushConstants _radixPC;

[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> globalHistogram;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> partitionHistogram;

groupshared uint reduction;
groupshared uint intermediate[MAX_SUBGROUP_SIZE];

[shader("compute")]
[numthreads(WORKGROUP_SIZE)]
void main(uint3 groupThreadID: SV_GroupThreadID, uint3 groupId: SV_GroupID,
          uint groupIndex: SV_GroupIndex)
{
    const uint pass = _radixPC.pass;
    const uint elementCount = _radixPC.elementCount;

    uint laneIndex = WaveGetLaneIndex();  // 0..31
    uint laneCount = WaveGetLaneCount();  // 32
    uint waveIndex = groupIndex / laneCount;
    uint waveCount = WORKGROUP_SIZE / laneCount;
    uint index = waveIndex * laneCount + laneIndex;

    uint radix = groupId.x;

    uint partitionCount = (elementCount + PARTITION_SIZE - 1) / PARTITION_SIZE;

    if (index == 0) {
        reduction = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint i = 0; WORKGROUP_SIZE * i < partitionCount; ++i) {
        uint partitionIndex = WORKGROUP_SIZE * i + index;
        uint value =
            partitionIndex < partitionCount ? partitionHistogram[RADIX * partitionIndex + radix] : 0;
        uint excl = WavePrefixSum(value) + reduction;
        uint sum = WaveActiveSum(value);

        if (WaveIsFirstLane()) {
            intermediate[waveIndex] = sum;
        }
        GroupMemoryBarrierWithGroupSync();

        if (index < waveCount) {
            uint excl = WavePrefixSum(intermediate[index]);
            uint sum = WaveActiveSum(intermediate[index]);
            intermediate[index] = excl;

            if (index == 0) {
                reduction += sum;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        if (partitionIndex < partitionCount) {
            excl += intermediate[waveIndex];
            partitionHistogram[RADIX * partitionIndex + radix] = excl;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (radix == 0) {
        // one workgroup is responsible for global histogram prefix sum
        if (index < RADIX) {
            uint value = globalHistogram[RADIX * pass + index];
            uint excl = WavePrefixSum(value);
            uint sum = WaveActiveSum(value);

            if (WaveIsFirstLane()) {
                intermediate[waveIndex] = sum;
            }
            GroupMemoryBarrierWithGroupSync();

            if (index < RADIX / laneCount) {
                uint excl = WavePrefixSum(intermediate[index]);
                intermediate[index] = excl;
            }
            GroupMemoryBarrierWithGroupSync();

            excl += intermediate[waveIndex];
            globalHistogram[RADIX * pass + index] = excl;
        }
    }
}
