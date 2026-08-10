#pragma once

#include "ModelShadowWork.h"

#include <Renderer/Descriptors/BufferDesc.h>

namespace Renderer
{
    class Renderer;
}

namespace ShadowRendering
{
    // Owns GPU-side queues, counters, indirect arguments, and diagnostic readback for model shadow work.
    // The bounded buffers let SVSM culling feed static and dynamic raster paths without CPU-authored draws.
    class ModelShadowWorkResources
    {
      public:
        explicit ModelShadowWorkResources(Renderer::Renderer* renderer);
        ~ModelShadowWorkResources();

        bool EnsureCapacity(u32 capacity);
        void ReadbackStats(u8 frameIndex);
        void MarkSubmitted(u8 frameIndex) { _hasReadback[frameIndex] = true; }

        Renderer::BufferID GetChunkQueue(u8 frameIndex) const { return _chunkQueues[frameIndex]; }
        Renderer::BufferID GetChunkArguments(u8 frameIndex) const { return _chunkArguments[frameIndex]; }
        Renderer::BufferID GetQueue(u32 queueIndex, u8 frameIndex) const { return _queues[frameIndex][queueIndex]; }
        Renderer::BufferID GetRecords(u8 frameIndex) const { return _records[frameIndex]; }
        Renderer::BufferID GetArguments(u8 frameIndex) const { return _arguments[frameIndex]; }
        Renderer::BufferID GetStatsBuffer(u8 frameIndex) const { return _statsBuffers[frameIndex]; }
        Renderer::BufferID GetStatsReadback(u8 frameIndex) const { return _statsReadbacks[frameIndex]; }
        u32 GetCapacity() const { return _capacity; }
        u32 GetGeneration() const { return _generation; }
        const ModelShadowStats& GetStats() const { return _stats; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        Renderer::BufferID _chunkQueues[MODEL_SHADOW_FRAME_COUNT] = {};
        Renderer::BufferID _chunkArguments[MODEL_SHADOW_FRAME_COUNT] = {};
        Renderer::BufferID _queues[MODEL_SHADOW_FRAME_COUNT][MODEL_SHADOW_QUEUE_COUNT] = {};
        Renderer::BufferID _records[MODEL_SHADOW_FRAME_COUNT] = {};
        Renderer::BufferID _arguments[MODEL_SHADOW_FRAME_COUNT] = {};
        Renderer::BufferID _statsBuffers[MODEL_SHADOW_FRAME_COUNT] = {};
        Renderer::BufferID _statsReadbacks[MODEL_SHADOW_FRAME_COUNT] = {};
        ModelShadowStats _stats;
        u32 _capacity = 0;
        u32 _generation = 0;
        bool _hasReadback[MODEL_SHADOW_FRAME_COUNT] = {};
    };
} // namespace ShadowRendering
