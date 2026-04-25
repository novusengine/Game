#include "RadixSort.h"

#include "Game-Lib/Rendering/GameRenderer.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraphBuilder.h>
#include <Renderer/CommandList.h>

#include <algorithm>

// Mirror the shader-side constants in Source/Shaders/Shaders/Sorting/Radix/Constants.inc.slang.
static constexpr u32 RADIX              = 256;
static constexpr u32 WORKGROUP_SIZE     = 512;
static constexpr u32 PARTITION_DIVISION = 8;
static constexpr u32 PARTITION_SIZE     = PARTITION_DIVISION * WORKGROUP_SIZE; // 4096
static constexpr u32 NUM_RADIX_PASSES   = 4;  // 4 passes * 8 bits = 32-bit key

RadixSort::RadixSort()
    : _upsweepFromPingSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _upsweepFromPongSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _spineSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _downsweepPingToPongSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _downsweepPongToPingSet(Renderer::DescriptorSetSlot::PER_PASS)
{
}

void RadixSort::Init(Renderer::Renderer* renderer, GameRenderer* gameRenderer, u32 maxKeyCount)
{
    _renderer     = renderer;
    _gameRenderer = gameRenderer;

    CreatePipelines();
    AllocateFixedScratch();

    _upsweepFromPingSet.RegisterPipeline(_renderer, _upsweepPipeline);
    _upsweepFromPingSet.Init(_renderer);
    _upsweepFromPongSet.RegisterPipeline(_renderer, _upsweepPipeline);
    _upsweepFromPongSet.Init(_renderer);

    _spineSet.RegisterPipeline(_renderer, _spinePipeline);
    _spineSet.Init(_renderer);

    _downsweepPingToPongSet.RegisterPipeline(_renderer, _downsweepPipeline);
    _downsweepPingToPongSet.Init(_renderer);
    _downsweepPongToPingSet.RegisterPipeline(_renderer, _downsweepPipeline);
    _downsweepPongToPingSet.Init(_renderer);

    AllocateKeyCountScratch(maxKeyCount);
    BindAllDescriptorSets();

    _initialized = true;
}

void RadixSort::CreatePipelines()
{
    // Upsweep + Spine: single permutation.
    auto loadNoPermutation = [&](const char* shaderPath, Renderer::ComputePipelineID& out, const char* debugName)
    {
        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(
            Renderer::GetShaderEntryNameHash(shaderPath, {}),
            shaderPath);

        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName     = debugName;
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        out = _renderer->CreatePipeline(pipelineDesc);
    };

    loadNoPermutation("Sorting/Radix/Upsweep.cs", _upsweepPipeline, "RadixSort.Upsweep");
    loadNoPermutation("Sorting/Radix/Spine.cs",   _spinePipeline,   "RadixSort.Spine");

    // Downsweep: compile with KEY_VALUE=1 permutation (we always sort key+value pairs).
    {
        std::vector<Renderer::PermutationField> permutation = {
            { "KEY_VALUE", "1" }
        };
        const char* shaderPath = "Sorting/Radix/Downsweep.cs";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(
            Renderer::GetShaderEntryNameHash(shaderPath, permutation),
            shaderPath);

        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName     = "RadixSort.Downsweep.KV";
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
        _downsweepPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
}

void RadixSort::AllocateFixedScratch()
{
    // globalHistogram: one u32[256] per radix pass = 4 * 256 * 4 bytes = 4 KiB. Zeroed by
    // FillBuffer at the start of each sort (globalHistogram is accumulated then scanned per pass).
    Renderer::BufferDesc desc;
    desc.name  = "RadixSort.GlobalHistogram";
    desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
    desc.size  = NUM_RADIX_PASSES * RADIX * sizeof(u32);
    _globalHistogram = _renderer->CreateBuffer(desc);
}

void RadixSort::AllocateKeyCountScratch(u32 newMaxKeyCount)
{
    const u8 sortScratchUsage = Renderer::BufferUsage::STORAGE_BUFFER
                              | Renderer::BufferUsage::TRANSFER_DESTINATION;
    const u8 identityUsage    = Renderer::BufferUsage::STORAGE_BUFFER
                              | Renderer::BufferUsage::TRANSFER_SOURCE;

    Renderer::BufferDesc desc;
    desc.usage = sortScratchUsage;
    desc.size  = static_cast<u64>(newMaxKeyCount) * sizeof(u32);

    desc.name = "RadixSort.SortKeys";
    _sortKeys = _renderer->CreateBuffer(_sortKeys, desc);

    desc.name = "RadixSort.WriteKeys";
    _writeKeys = _renderer->CreateBuffer(_writeKeys, desc);

    desc.name = "RadixSort.SortValues";
    _sortValues = _renderer->CreateBuffer(_sortValues, desc);

    desc.name = "RadixSort.WriteValues";
    _writeValues = _renderer->CreateBuffer(_writeValues, desc);

    // partitionHistogram: u32[maxPartitions * 256]. Written by upsweep, scanned by spine, read by
    // downsweep. No TRANSFER usage needed (never copied to/from).
    const u32 maxPartitions = (newMaxKeyCount + PARTITION_SIZE - 1) / PARTITION_SIZE;
    desc.name  = "RadixSort.PartitionHistogram";
    desc.usage = Renderer::BufferUsage::STORAGE_BUFFER;
    desc.size  = static_cast<u64>(std::max(maxPartitions, 1u)) * RADIX * sizeof(u32);
    _partitionHistogram = _renderer->CreateBuffer(_partitionHistogram, desc);

    // Identity values buffer: [0, 1, 2, ..., newMaxKeyCount-1]. CopyBuffer source only.
    desc.name  = "RadixSort.IdentityValues";
    desc.usage = identityUsage;
    desc.size  = static_cast<u64>(newMaxKeyCount) * sizeof(u32);
    _identityValues = _renderer->CreateAndFillBuffer(_identityValues, desc,
        [newMaxKeyCount](void* mapped, size_t) {
            u32* p = static_cast<u32*>(mapped);
            for (u32 i = 0; i < newMaxKeyCount; ++i)
                p[i] = i;
        });

    _maxKeyCount = newMaxKeyCount;
}

void RadixSort::BindAllDescriptorSets()
{
    // Binding numbers match the shader's [[vk::binding(N, PER_PASS)]]. Binding 0 is not used --
    // we dropped the elementCounts buffer in favour of a push constant.
    _upsweepFromPingSet.Bind("globalHistogram"_h,    _globalHistogram);
    _upsweepFromPingSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _upsweepFromPingSet.Bind("keys"_h,               _sortKeys);

    _upsweepFromPongSet.Bind("globalHistogram"_h,    _globalHistogram);
    _upsweepFromPongSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _upsweepFromPongSet.Bind("keys"_h,               _writeKeys);

    _spineSet.Bind("globalHistogram"_h,    _globalHistogram);
    _spineSet.Bind("partitionHistogram"_h, _partitionHistogram);

    // Downsweep ping->pong: read sortKeys/sortValues, write writeKeys/writeValues.
    _downsweepPingToPongSet.Bind("globalHistogram"_h,    _globalHistogram);
    _downsweepPingToPongSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _downsweepPingToPongSet.Bind("keysIn"_h,             _sortKeys);
    _downsweepPingToPongSet.Bind("keysOut"_h,            _writeKeys);
    _downsweepPingToPongSet.Bind("valuesIn"_h,           _sortValues);
    _downsweepPingToPongSet.Bind("valuesOut"_h,          _writeValues);

    // Downsweep pong->ping: the reverse.
    _downsweepPongToPingSet.Bind("globalHistogram"_h,    _globalHistogram);
    _downsweepPongToPingSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _downsweepPongToPingSet.Bind("keysIn"_h,             _writeKeys);
    _downsweepPongToPingSet.Bind("keysOut"_h,            _sortKeys);
    _downsweepPongToPingSet.Bind("valuesIn"_h,           _writeValues);
    _downsweepPongToPingSet.Bind("valuesOut"_h,          _sortValues);
}

void RadixSort::EnsureCapacity(u32 requiredMaxKeyCount)
{
    if (!_initialized || requiredMaxKeyCount <= _maxKeyCount)
        return;

    const u32 newCap = std::max(_maxKeyCount * 2, requiredMaxKeyCount);

    AllocateKeyCountScratch(newCap);

    // Rebind every descriptor set that references the resized buffers. Safe here because the
    // caller must invoke EnsureCapacity CPU-side (outside any render-graph execute) -- the
    // previous frame's command list either isn't submitted yet or has already released the old
    // IDs (deferred-destroyed via QueueDestroyBuffer inside CreateBuffer(existing, desc)).
    _upsweepFromPingSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _upsweepFromPingSet.Bind("keys"_h,               _sortKeys);

    _upsweepFromPongSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _upsweepFromPongSet.Bind("keys"_h,               _writeKeys);

    _spineSet.Bind("partitionHistogram"_h, _partitionHistogram);

    _downsweepPingToPongSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _downsweepPingToPongSet.Bind("keysIn"_h,             _sortKeys);
    _downsweepPingToPongSet.Bind("keysOut"_h,            _writeKeys);
    _downsweepPingToPongSet.Bind("valuesIn"_h,           _sortValues);
    _downsweepPingToPongSet.Bind("valuesOut"_h,          _writeValues);

    _downsweepPongToPingSet.Bind("partitionHistogram"_h, _partitionHistogram);
    _downsweepPongToPingSet.Bind("keysIn"_h,             _writeKeys);
    _downsweepPongToPingSet.Bind("keysOut"_h,            _sortKeys);
    _downsweepPongToPingSet.Bind("valuesIn"_h,           _writeValues);
    _downsweepPongToPingSet.Bind("valuesOut"_h,          _sortValues);
}

RadixSort::PassResources RadixSort::RegisterPass(Renderer::RenderGraphBuilder& builder)
{
    using BufferUsage = Renderer::BufferPassUsage;

    PassResources res;

    // sortKeys/sortValues are CopyBuffer destinations THEN compute read/write within the sort,
    // so they need TRANSFER | COMPUTE.
    res.sortKeys   = builder.Write(_sortKeys,   BufferUsage::TRANSFER | BufferUsage::COMPUTE);
    res.sortValues = builder.Write(_sortValues, BufferUsage::TRANSFER | BufferUsage::COMPUTE);
    builder.Write(_writeKeys,    BufferUsage::COMPUTE);
    builder.Write(_writeValues,  BufferUsage::COMPUTE);
    res.identityValues = builder.Read(_identityValues, BufferUsage::TRANSFER);

    // globalHistogram is zeroed via FillBuffer (TRANSFER) then read/written by compute.
    res.globalHistogram = builder.Write(_globalHistogram, BufferUsage::TRANSFER | BufferUsage::COMPUTE);
    builder.Write(_partitionHistogram,  BufferUsage::COMPUTE);

    res.upsweepFromPing     = builder.Use(_upsweepFromPingSet);
    res.upsweepFromPong     = builder.Use(_upsweepFromPongSet);
    res.spine               = builder.Use(_spineSet);
    res.downsweepPingToPong = builder.Use(_downsweepPingToPongSet);
    res.downsweepPongToPing = builder.Use(_downsweepPongToPingSet);

    return res;
}

void RadixSort::RecordSort(Renderer::CommandList& commandList, u8 frameIndex,
                           const PassResources& passRes, u32 numKeys)
{
    if (numKeys == 0)
        return;

    const u32 partitionCount = (numKeys + PARTITION_SIZE - 1) / PARTITION_SIZE;

    struct RadixPC { u32 pass; u32 elementCount; };

    commandList.PushMarker("RadixSort", Color::Green);

    // Zero the 4 KiB global histogram before each sort. Spine writes a prefix-sum over the per-
    // pass counts accumulated by upsweep into this buffer; we need a clean slate.
    commandList.FillBuffer(passRes.globalHistogram, 0, NUM_RADIX_PASSES * RADIX * sizeof(u32), 0);
    commandList.BufferBarrier(passRes.globalHistogram, Renderer::BufferPassUsage::TRANSFER);

    for (u32 pass = 0; pass < NUM_RADIX_PASSES; ++pass)
    {
        const bool fromPing = (pass & 1) == 0;
        RadixPC pc{ pass, numKeys };

        // Upsweep: builds per-partition histograms + global histogram for this pass.
        commandList.BeginPipeline(_upsweepPipeline);
        commandList.PushConstant(&pc, 0, sizeof(pc));
        commandList.BindDescriptorSet(fromPing ? passRes.upsweepFromPing : passRes.upsweepFromPong, frameIndex);
        commandList.Dispatch(partitionCount, 1, 1);
        commandList.EndPipeline(_upsweepPipeline);

        commandList.BufferBarrier(passRes.sortKeys, Renderer::BufferPassUsage::COMPUTE);

        // Spine: prefix-scan the per-partition histograms (one group per radix bin) + prefix-scan
        // the global histogram for this pass (bin 0's group handles that).
        commandList.BeginPipeline(_spinePipeline);
        commandList.PushConstant(&pc, 0, sizeof(pc));
        commandList.BindDescriptorSet(passRes.spine, frameIndex);
        commandList.Dispatch(RADIX, 1, 1);
        commandList.EndPipeline(_spinePipeline);

        commandList.BufferBarrier(passRes.sortKeys, Renderer::BufferPassUsage::COMPUTE);

        // Downsweep: scatter keys and values to their globally sorted positions for this pass.
        commandList.BeginPipeline(_downsweepPipeline);
        commandList.PushConstant(&pc, 0, sizeof(pc));
        commandList.BindDescriptorSet(fromPing ? passRes.downsweepPingToPong : passRes.downsweepPongToPing, frameIndex);
        commandList.Dispatch(partitionCount, 1, 1);
        commandList.EndPipeline(_downsweepPipeline);

        if (pass + 1 < NUM_RADIX_PASSES)
            commandList.BufferBarrier(passRes.sortKeys, Renderer::BufferPassUsage::COMPUTE);
    }

    commandList.PopMarker();
}
