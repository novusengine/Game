#pragma once

#include "ModelViewWork.h"

#include <Renderer/Descriptors/BufferDesc.h>

namespace Renderer
{
    class Renderer;
}

namespace ModelView
{
    // Owns GPU-side append queues, indirect arguments, counters, and readback for one model View.
    // These buffers let culling produce bounded raster work without CPU-authored meshlet lists.
    class ModelViewWorkResources
    {
      public:
        static constexpr u32 FRAME_COUNT = MODEL_VIEW_FRAME_COUNT;
        explicit ModelViewWorkResources(Renderer::Renderer* renderer);
        ~ModelViewWorkResources();

        bool EnsureQueueCapacity(u32 meshletCount);
        void ReadbackStats(u8 frameIndex);
        void MarkSubmitted(u8 frameIndex) { _hasReadback[frameIndex] = true; }

        Renderer::BufferID GetQueue(u32 rasterClass, u8 frameIndex) const { return _queues[frameIndex][rasterClass]; }
        Renderer::BufferID GetVisibilityRecords(u8 frameIndex) const { return _visibilityRecords[frameIndex]; }
        Renderer::BufferID GetChunkQueue(u8 frameIndex) const { return _chunkQueues[frameIndex]; }
        Renderer::BufferID GetChunkArguments(u8 frameIndex) const { return _chunkArguments[frameIndex]; }
        Renderer::BufferID GetArguments(u8 frameIndex) const { return _arguments[frameIndex]; }
        Renderer::BufferID GetStatsBuffer(u8 frameIndex) const { return _statsBuffers[frameIndex]; }
        Renderer::BufferID GetStatsReadback(u8 frameIndex) const { return _statsReadbacks[frameIndex]; }
        u32 GetQueueCapacity() const { return _queueCapacity; }
        u32 GetQueueGeneration() const { return _queueGeneration; }
        const WorkStats& GetStats() const { return _stats; }
        const u32* GetArgumentSnapshot() const { return _argumentSnapshot; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        Renderer::BufferID _queues[FRAME_COUNT][MODEL_RASTER_CLASS_COUNT] = {};
        Renderer::BufferID _visibilityRecords[FRAME_COUNT] = {};
        Renderer::BufferID _chunkQueues[FRAME_COUNT] = {};
        Renderer::BufferID _chunkArguments[FRAME_COUNT] = {};
        Renderer::BufferID _arguments[FRAME_COUNT] = {};
        Renderer::BufferID _statsBuffers[FRAME_COUNT] = {};
        Renderer::BufferID _statsReadbacks[FRAME_COUNT] = {};
        WorkStats _stats;
        u32 _argumentSnapshot[MODEL_RASTER_CLASS_COUNT * MODEL_DISPATCH_ARGUMENT_COUNT] = {};
        u32 _queueCapacity = 0;
        u32 _queueGeneration = 0;
        bool _hasReadback[FRAME_COUNT] = {};
    };
} // namespace ModelView
