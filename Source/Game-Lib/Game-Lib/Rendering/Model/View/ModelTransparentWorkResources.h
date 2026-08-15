#pragma once

#include "ModelTransparentWork.h"
#include "ModelViewWork.h"

#include <Renderer/Descriptors/BufferDesc.h>

namespace Renderer { class Renderer; }

namespace ModelView
{
    // Owns GPU-side transparent chunk queues, execution-group records, indirect arguments, and readback for one View.
    // The resources let one non-temporal cull feed all forward Material execution groups without CPU-authored draws.
    class ModelTransparentWorkResources
    {
      public:
        explicit ModelTransparentWorkResources(Renderer::Renderer* renderer);
        ~ModelTransparentWorkResources();

        bool EnsureCapacity(u32 sceneMeshletCapacity);
        void ReadbackStats(u8 frameIndex);
        void MarkSubmitted(u8 frameIndex) { _hasReadback[frameIndex] = true; }

        Renderer::BufferID GetChunkQueue(u8 frameIndex) const { return _chunkQueues[frameIndex]; }
        Renderer::BufferID GetChunkArguments(u8 frameIndex) const { return _chunkArguments[frameIndex]; }
        Renderer::BufferID GetVisibilityRecords(u8 frameIndex) const { return _visibilityRecords[frameIndex]; }
        Renderer::BufferID GetArguments(u8 frameIndex) const { return _arguments[frameIndex]; }
        Renderer::BufferID GetStatsBuffer(u8 frameIndex) const { return _statsBuffers[frameIndex]; }
        Renderer::BufferID GetStatsReadback(u8 frameIndex) const { return _statsReadbacks[frameIndex]; }
        u32 GetChunkCapacity() const { return _chunkCapacity; }
        u32 GetBinCapacity() const { return _binCapacity; }
        u32 GetGeneration() const { return _generation; }
        const TransparentWorkStats& GetStats() const { return _stats; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        Renderer::BufferID _chunkQueues[MODEL_TRANSPARENT_FRAME_COUNT] = {};
        Renderer::BufferID _chunkArguments[MODEL_TRANSPARENT_FRAME_COUNT] = {};
        Renderer::BufferID _visibilityRecords[MODEL_TRANSPARENT_FRAME_COUNT] = {};
        Renderer::BufferID _arguments[MODEL_TRANSPARENT_FRAME_COUNT] = {};
        Renderer::BufferID _statsBuffers[MODEL_TRANSPARENT_FRAME_COUNT] = {};
        Renderer::BufferID _statsReadbacks[MODEL_TRANSPARENT_FRAME_COUNT] = {};
        TransparentWorkStats _stats;
        u32 _chunkCapacity = 0;
        u32 _binCapacity = 0;
        u32 _generation = 0;
        bool _hasReadback[MODEL_TRANSPARENT_FRAME_COUNT] = {};
    };
} // namespace ModelView
