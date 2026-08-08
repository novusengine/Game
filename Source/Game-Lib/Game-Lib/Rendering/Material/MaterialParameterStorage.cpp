#include "MaterialParameterStorage.h"

#include <Renderer/Renderer.h>

#include <xxhash/xxhash64.h>

#include <cstring>
#include <limits>

namespace MaterialLoading
{
    MaterialParameterStorage::MaterialParameterStorage(bool validateTransfers)
        : _bytes(validateTransfers)
    {
        _bytes.SetDebugName("Material Parameter Data");
        _bytes.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    bool MaterialParameterStorage::Append(std::span<const u8> bytes, u32 alignment, u32& outOffset)
    {
        ZoneScopedN("MaterialParameterStorage::Append");

        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            return false;

        const u64 hash = XXHash64::hash(bytes.data(), bytes.size(), bytes.size());
        const auto existing = _hashToBlocks.find(hash);
        if (existing != _hashToBlocks.end())
        {
            for (const Block& block : existing->second)
            {
                if (block.size != bytes.size() || block.offset % alignment != 0)
                    continue;

                bool matches = true;
                for (u32 index = 0; index < block.size; ++index)
                    matches &= _bytes[block.offset + index] == bytes[index];
                if (matches)
                {
                    outOffset = block.offset;
                    ++_dedupHits;
                    return true;
                }
            }
        }

        const u64 alignedOffset = (static_cast<u64>(_bytes.Count()) + alignment - 1u) & ~(static_cast<u64>(alignment) - 1u);
        const u64 requiredSize = alignedOffset + bytes.size();
        if (requiredSize > std::numeric_limits<u32>::max())
            return false;

        const u32 appendSize = static_cast<u32>(requiredSize) - _bytes.Count();
        _bytes.Reserve(appendSize);
        _bytes.AddCount(appendSize);
        outOffset = static_cast<u32>(alignedOffset);
        if (!bytes.empty())
        {
            std::memcpy(&_bytes[outOffset], bytes.data(), bytes.size());
        }

        _hashToBlocks[hash].push_back({outOffset, static_cast<u32>(bytes.size())});
        ++_uniqueBlocks;
        return true;
    }

    void MaterialParameterStorage::SyncToGPU(Renderer::Renderer* renderer)
    {
        _bufferGrowths += _bytes.SyncToGPU(renderer) ? 1u : 0u;
    }

    MaterialParameterStorageStats MaterialParameterStorage::GetStats() const
    {
        return {
            .uniqueBlocks = _uniqueBlocks,
            .dedupHits = _dedupHits,
            .bufferGrowths = _bufferGrowths,
            .usedBytes = _bytes.UsedBytes(),
            .reservedBytes = _bytes.TotalBytes()
        };
    }
} // namespace MaterialLoading
