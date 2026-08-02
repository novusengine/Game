#pragma once

#include <Renderer/GPUVector.h>

#include <robinhood/robinhood.h>

#include <span>
#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace MaterialLoading
{
    struct MaterialParameterStorageStats
    {
        u32 uniqueBlocks = 0;
        u32 dedupHits = 0;
        u32 bufferGrowths = 0;
        u64 usedBytes = 0;
        u64 reservedBytes = 0;
    };

    // Deduplicates aligned material parameter blocks on the CPU and stores them in a GPU-side buffer.
    class MaterialParameterStorage
    {
      public:
        explicit MaterialParameterStorage(bool validateTransfers = false);

        bool Append(std::span<const u8> bytes, u32 alignment, u32& outOffset);
        void SyncToGPU(Renderer::Renderer* renderer);

        u8 GetByte(u32 offset) const { return _bytes[offset]; }
        const Renderer::GPUVector<u8, 256>& GetBuffer() const { return _bytes; }
        MaterialParameterStorageStats GetStats() const;

      private:
        struct Block
        {
            u32 offset = 0;
            u32 size = 0;
        };

        Renderer::GPUVector<u8, 256> _bytes;
        robin_hood::unordered_map<u64, std::vector<Block>> _hashToBlocks;
        u32 _uniqueBlocks = 0;
        u32 _dedupHits = 0;
        u32 _bufferGrowths = 0;
    };
} // namespace MaterialLoading
