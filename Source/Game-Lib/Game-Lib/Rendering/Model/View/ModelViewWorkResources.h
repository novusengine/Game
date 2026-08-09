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
        bool EnsureCullReasonCapacity(u32 meshletCount);
        bool EnsureHistoryCapacity(u32 instanceCount, u32 meshletHistoryWords);
        void ReadbackStats(u8 frameIndex);
        void MarkSubmitted(u8 frameIndex) { _hasReadback[frameIndex] = true; }

        Renderer::BufferID GetQueue(u32 rasterClass, u8 frameIndex) const { return _queues[frameIndex][rasterClass]; }
        Renderer::BufferID GetVisibilityRecords(u8 frameIndex) const { return _visibilityRecords[frameIndex]; }
        Renderer::BufferID GetCullReasons(u8 frameIndex) const { return _cullReasons[frameIndex]; }
        Renderer::BufferID GetChunkQueue(u8 frameIndex) const { return _chunkQueues[frameIndex]; }
        Renderer::BufferID GetChunkArguments(u8 frameIndex) const { return _chunkArguments[frameIndex]; }
        Renderer::BufferID GetArguments(u8 frameIndex) const { return _arguments[frameIndex]; }
        Renderer::BufferID GetStatsBuffer(u8 frameIndex) const { return _statsBuffers[frameIndex]; }
        Renderer::BufferID GetStatsReadback(u8 frameIndex) const { return _statsReadbacks[frameIndex]; }
        Renderer::BufferID GetInstanceVisibility(u8 frameIndex) const { return _instanceVisibility[frameIndex]; }
        Renderer::BufferID GetMeshletHistory(u8 frameIndex) const { return _meshletHistory[frameIndex]; }
        Renderer::BufferID GetSurvivorQueue(u8 frameIndex) const { return _survivorQueues[frameIndex]; }
        Renderer::BufferID GetSurvivorArguments(u8 frameIndex) const { return _survivorArguments[frameIndex]; }
        u32 GetQueueCapacity() const { return _queueCapacity; }
        u32 GetQueueGeneration() const { return _queueGeneration; }
        u32 GetHistoryGeneration() const { return _historyGeneration; }
        u32 GetInstanceVisibilityWords() const { return _instanceVisibilityWords; }
        u32 GetMeshletHistoryWords() const { return _meshletHistoryWords; }
        const WorkStats& GetStats() const { return _stats; }
        const u32* GetArgumentSnapshot() const { return _argumentSnapshot; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        Renderer::BufferID _queues[FRAME_COUNT][MODEL_RASTER_CLASS_COUNT] = {};
        Renderer::BufferID _visibilityRecords[FRAME_COUNT] = {};
        Renderer::BufferID _cullReasons[FRAME_COUNT] = {};
        Renderer::BufferID _chunkQueues[FRAME_COUNT] = {};
        Renderer::BufferID _chunkArguments[FRAME_COUNT] = {};
        Renderer::BufferID _arguments[FRAME_COUNT] = {};
        Renderer::BufferID _statsBuffers[FRAME_COUNT] = {};
        Renderer::BufferID _statsReadbacks[FRAME_COUNT] = {};
        Renderer::BufferID _instanceVisibility[FRAME_COUNT] = {};
        Renderer::BufferID _meshletHistory[FRAME_COUNT] = {};
        Renderer::BufferID _survivorQueues[FRAME_COUNT] = {};
        Renderer::BufferID _survivorArguments[FRAME_COUNT] = {};
        WorkStats _stats;
        u32 _argumentSnapshot[MODEL_RASTER_CLASS_COUNT * MODEL_DISPATCH_ARGUMENT_COUNT] = {};
        u32 _queueCapacity = 0;
        u32 _cullReasonCapacity = 0;
        u32 _queueGeneration = 0;
        u32 _historyGeneration = 0;
        u32 _instanceVisibilityWords = 0;
        u32 _meshletHistoryWords = 0;
        bool _hasReadback[FRAME_COUNT] = {};
    };
} // namespace ModelView
