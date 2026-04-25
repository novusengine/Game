#pragma once
#include <Base/Types.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/BufferDesc.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>
#include <Renderer/RenderPassResources.h>
#include <Renderer/DescriptorSetResource.h>

namespace Renderer
{
    class Renderer;
    class RenderGraphBuilder;
    class CommandList;
}

class GameRenderer;

// GPU u32 radix sort (reduce-then-scan), 8 bits per pass => 4 passes per u32 key, 3 dispatches per
// pass => 12 dispatches per sort. Port of https://github.com/jaesung-cs/vulkan_radix_sort.
//
// Architecture: a single descriptor-set family shared across every sort in the frame. Inputs are
// staged into `sortKeys`/`sortValues` via vkCmdCopyBuffer before each sort. All descriptor sets
// bind fixed scratch buffers, are bound once at Init, and never rebind at runtime -- keeps SSBO
// descriptor-pool cost flat regardless of how many sorts run per frame.
//
// Usage:
//   1. Call `RegisterPass(builder)` once inside a render-graph pass's onSetup lambda.
//   2. For each sort inside onExecute:
//        a. CopyBuffer caller's keys  -> passRes.sortKeys.
//        b. CopyBuffer identityValues -> passRes.sortValues (seed payload = [0..N-1]).
//        c. Barriers from TRANSFER to COMPUTE on sortKeys / sortValues.
//        d. Call `RecordSort(cl, frameIndex, passRes, numKeys)`.
//      After RecordSort returns, passRes.sortKeys/sortValues contain the sorted pairs in place
//      (4 passes = even count, so the ping-pong lands back on the input buffers).
//
// Growth: `EnsureCapacity(N)` reallocates size-dependent scratch to at least N keys and rebinds
// the 5 sort descriptor sets. Must be called CPU-side (outside render-graph execution).
class RadixSort
{
public:
    struct PassResources
    {
        Renderer::DescriptorSetResource upsweepFromPing;
        Renderer::DescriptorSetResource upsweepFromPong;
        Renderer::DescriptorSetResource spine;
        Renderer::DescriptorSetResource downsweepPingToPong;
        Renderer::DescriptorSetResource downsweepPongToPing;

        // Shared-scratch handles the caller copies INTO before calling RecordSort.
        Renderer::BufferMutableResource sortKeys;       // u32[maxN]
        Renderer::BufferMutableResource sortValues;     // u32[maxN]

        // Source the caller copies FROM when seeding sortValues = [0..N-1].
        Renderer::BufferResource        identityValues; // u32[maxN]

        // Internal scratch exposed only so RecordSort can FillBuffer / BufferBarrier it.
        // Callers shouldn't touch this.
        Renderer::BufferMutableResource globalHistogram;
    };

    RadixSort();

    void Init(Renderer::Renderer* renderer, GameRenderer* gameRenderer, u32 maxKeyCount);

    PassResources RegisterPass(Renderer::RenderGraphBuilder& builder);

    void RecordSort(Renderer::CommandList& commandList, u8 frameIndex,
                    const PassResources& passRes, u32 numKeys);

    void EnsureCapacity(u32 requiredMaxKeyCount);

    Renderer::BufferID GetSortValuesBuffer() const { return _sortValues; }
    u32 GetMaxKeyCount() const { return _maxKeyCount; }

private:
    void CreatePipelines();
    void AllocateFixedScratch();                       // globalHistogram (fixed, 4 KiB)
    void AllocateKeyCountScratch(u32 newMaxKeyCount);  // sortKeys/sortValues/writeKeys/writeValues/identityValues/partitionHistogram
    void BindAllDescriptorSets();

private:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;

    u32 _maxKeyCount = 0;

    // --- Pipelines (3) --------------------------------------------------------------------
    Renderer::ComputePipelineID _upsweepPipeline;
    Renderer::ComputePipelineID _spinePipeline;
    Renderer::ComputePipelineID _downsweepPipeline; // compiled with KEY_VALUE=1 permutation

    // --- 5 shared descriptor sets (bound once, rebound only by EnsureCapacity) ------------
    Renderer::DescriptorSet _upsweepFromPingSet;
    Renderer::DescriptorSet _upsweepFromPongSet;
    Renderer::DescriptorSet _spineSet;
    Renderer::DescriptorSet _downsweepPingToPongSet;
    Renderer::DescriptorSet _downsweepPongToPingSet;

    // --- Scratch buffers ------------------------------------------------------------------
    // Size-dependent on _maxKeyCount (resized by EnsureCapacity):
    Renderer::BufferID _sortKeys       = Renderer::BufferID::Invalid(); // u32[maxN]
    Renderer::BufferID _sortValues     = Renderer::BufferID::Invalid(); // u32[maxN]
    Renderer::BufferID _writeKeys      = Renderer::BufferID::Invalid(); // u32[maxN] ping-pong
    Renderer::BufferID _writeValues    = Renderer::BufferID::Invalid(); // u32[maxN] ping-pong
    Renderer::BufferID _identityValues = Renderer::BufferID::Invalid(); // u32[maxN] = [0..maxN-1]
    Renderer::BufferID _partitionHistogram = Renderer::BufferID::Invalid(); // u32[maxPartitions * 256]

    // Fixed-size scratch (never resized):
    Renderer::BufferID _globalHistogram; // u32[4 * 256] = 4 KiB

    bool _initialized = false;
};
