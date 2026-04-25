// Reduce-then-scan radix sort -- upsweep pass.
// For each partition of PARTITION_SIZE keys, counts per-radix-bin histograms:
//   - Writes the per-partition histogram to partitionHistogram[radix][partitionIndex]
//   - Accumulates the per-pass global histogram via atomic add
// Dispatched with `(partitionCount, 1, 1)` groups, WORKGROUP_SIZE threads each.

#include "Sorting/Radix/Constants.inc.slang"

struct RadixPushConstants
{
    uint pass;          // 0..3 for 4 passes of 8 bits over a u32 key
    uint elementCount;  // total valid keys (the last partition may be partial)
};
[[vk::push_constant]] RadixPushConstants _radixPC;

[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> globalHistogram;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> partitionHistogram;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> keys;

groupshared uint localHistogram[RADIX];

[shader("compute")]
[numthreads(WORKGROUP_SIZE)]
void main(uint3 groupThreadID: SV_GroupThreadID, uint3 groupId: SV_GroupID)
{
    const uint pass = _radixPC.pass;
    const uint elementCount = _radixPC.elementCount;

    uint index = groupThreadID.x;
    uint partitionIndex = groupId.x;
    uint partitionStart = partitionIndex * PARTITION_SIZE;

    // discard all workgroup invocations
    if (partitionStart >= elementCount) {
        return;
    }

    if (index < RADIX) {
        localHistogram[index] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // local histogram
    for (int i = 0; i < PARTITION_DIVISION; ++i) {
        uint keyIndex = partitionStart + WORKGROUP_SIZE * i + index;
        uint key = keyIndex < elementCount ? keys[keyIndex] : 0xffffffff;
        uint radix = bitfieldExtract(key, 8 * pass, 8);
        __atomic_add(localHistogram[radix], 1, MemoryOrder.Relaxed);
    }
    GroupMemoryBarrierWithGroupSync();

    if (index < RADIX) {
        // set to partition histogram
        partitionHistogram[RADIX * partitionIndex + index] = localHistogram[index];

        // add to global histogram
        __atomic_add(globalHistogram[RADIX * pass + index], localHistogram[index], MemoryOrder.Relaxed);
    }
}
