#pragma once
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"
#include "Game-Lib/Rendering/Scene/StableRangeAllocator.h"

#include <Renderer/GPUVector.h>

#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace ModelScene
{
    struct GeometryGroupMaskStoreStats
    {
        u32 liveMasks = 0;
        u32 liveWords = 0;
        u32 allocatedWords = 0;
        u32 freeWords = 0;
    };

    // Owns CPU-side geometry-group mask allocations and their GPU-side packed bitset storage.
    // The masks reject disabled asset variants during culling before they produce rendering work.
    class GeometryGroupMaskStore
    {
      public:
        explicit GeometryGroupMaskStore(bool validateTransfers = false);

        void Reserve(u32 maskCount, u32 wordCount);
        RenderScenes::GeometryGroupMaskHandle Create(u32 groupCount, bool enabledByDefault = true);
        void Release(RenderScenes::GeometryGroupMaskHandle handle);
        bool SetEnabled(RenderScenes::GeometryGroupMaskHandle handle, u32 groupID, bool enabled);
        bool SetAll(RenderScenes::GeometryGroupMaskHandle handle, bool enabled);
        bool IsEnabled(RenderScenes::GeometryGroupMaskHandle handle, u32 groupID) const;
        void SyncToGPU(Renderer::Renderer* renderer);

        bool IsValid(RenderScenes::GeometryGroupMaskHandle handle) const;
        u32 GetOffset(RenderScenes::GeometryGroupMaskHandle handle) const;
        u32 GetWordCount(RenderScenes::GeometryGroupMaskHandle handle) const;
        GeometryGroupMaskStoreStats GetStats() const;
        const Renderer::GPUVector<u32>& GetMasks() const { return _masks; }

      private:
        struct Mask
        {
            RenderScenes::StableRange range;
            u32 groupCount = 0;
            bool alive = false;
        };

        Mask* GetMask(RenderScenes::GeometryGroupMaskHandle handle);
        const Mask* GetMask(RenderScenes::GeometryGroupMaskHandle handle) const;
        void EnsureWordCapacity(u32 count);

        Renderer::GPUVector<u32> _masks;
        RenderScenes::StableRangeAllocator _wordAllocator;
        std::vector<Mask> _records;
        std::vector<u32> _freeRecordIndices;
        u32 _liveMasks = 0;
        u32 _liveWords = 0;
    };
} // namespace ModelScene
