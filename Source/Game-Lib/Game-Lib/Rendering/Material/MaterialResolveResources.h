#pragma once

#include <Renderer/Descriptors/BufferDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/FrameResource.h>

namespace Renderer { class Renderer; }

namespace MaterialRendering
{
    inline constexpr u32 MATERIAL_EXECUTION_GROUP_COUNT = 6;
    inline constexpr u32 MATERIAL_TILE_SIZE = 8;
    inline constexpr u32 MATERIAL_DISPATCH_ARGUMENT_COUNT = 3;

    struct MaterialClassificationStats
    {
        u32 groupTileCounts[MATERIAL_EXECUTION_GROUP_COUNT] = {};
        u32 groupPixelCounts[MATERIAL_EXECUTION_GROUP_COUNT] = {};
    };

    // Owns GPU-side material-ID, tile-list, counter, and indirect-dispatch storage for one View.
    // These resources let material groups shade only tiles containing their pixels without CPU queue construction.
    class MaterialResolveResources
    {
      public:
        explicit MaterialResolveResources(Renderer::Renderer* renderer);
        ~MaterialResolveResources();

        bool EnsureTileCapacity(u32 tileCount);
        void ReadbackStats(u8 frameIndex);
        void MarkSubmitted(u8 frameIndex) { _hasReadback.Get(frameIndex) = true; }

        Renderer::ImageID GetMaterialIDs() const { return _materialIDs; }
        Renderer::BufferID GetTileQueue(u8 frameIndex) const { return _tileQueues.items[frameIndex % _tileQueues.Num]; }
        Renderer::BufferID GetCounters(u8 frameIndex) const { return _counters.items[frameIndex % _counters.Num]; }
        Renderer::BufferID GetArguments(u8 frameIndex) const { return _arguments.items[frameIndex % _arguments.Num]; }
        Renderer::BufferID GetReadback(u8 frameIndex) const { return _readbacks.items[frameIndex % _readbacks.Num]; }
        u32 GetTileCapacity() const { return _tileCapacity; }
        u32 GetGeneration() const { return _generation; }
        const MaterialClassificationStats& GetStats() const { return _stats; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        Renderer::ImageID _materialIDs = Renderer::ImageID::Invalid();
        FrameResource<Renderer::BufferID, 2> _tileQueues = {};
        FrameResource<Renderer::BufferID, 2> _counters = {};
        FrameResource<Renderer::BufferID, 2> _arguments = {};
        FrameResource<Renderer::BufferID, 2> _readbacks = {};
        MaterialClassificationStats _stats;
        u32 _tileCapacity = 0;
        u32 _generation = 0;
        FrameResource<bool, 2> _hasReadback = {};
    };
} // namespace MaterialRendering
