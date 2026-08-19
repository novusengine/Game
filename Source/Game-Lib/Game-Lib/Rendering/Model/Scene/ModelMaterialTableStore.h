#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"
#include "Game-Lib/Rendering/Scene/StableRangeAllocator.h"

#include <Renderer/GPUVector.h>

#include <robinhood/robinhood.h>

#include <span>
#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace ModelScene
{
    struct ModelMaterialTableStoreStats
    {
        u32 sharedTables = 0;
        u32 privateTables = 0;
        u32 liveEntries = 0;
        u32 allocatedEntries = 0;
        u32 freeEntries = 0;
    };

    // Owns CPU-side table metadata and the GPU-side material-instance table used by Scene model instances.
    // The tables permit per-instance material choices without duplicating model geometry.
    class ModelMaterialTableStore
    {
      public:
        explicit ModelMaterialTableStore(bool validateTransfers = false);

        void Reserve(u32 tableCount, u32 entryCount);
        RenderScenes::ModelMaterialTableHandle AcquireShared(std::span<const u32> materials);
        bool AddReference(RenderScenes::ModelMaterialTableHandle table);
        RenderScenes::ModelMaterialTableHandle CreatePrivate(RenderScenes::ModelMaterialTableHandle source);
        bool SetMaterial(RenderScenes::ModelMaterialTableHandle table, u32 slot, RenderAssets::MaterialInstanceHandle material);
        void Release(RenderScenes::ModelMaterialTableHandle table);
        void FlushFrees() { _entryAllocator.FlushFrees(); }
        void SyncToGPU(Renderer::Renderer* renderer);

        bool IsValid(RenderScenes::ModelMaterialTableHandle table) const;
        bool IsPrivate(RenderScenes::ModelMaterialTableHandle table) const;
        u32 GetOffset(RenderScenes::ModelMaterialTableHandle table) const;
        u32 GetCount(RenderScenes::ModelMaterialTableHandle table) const;
        u32 GetMaterial(RenderScenes::ModelMaterialTableHandle table, u32 slot) const;
        ModelMaterialTableStoreStats GetStats() const;
        const Renderer::GPUVector<u32>& GetEntries() const { return _entries; }

      private:
        struct Table
        {
            RenderScenes::StableRange range;
            u32 referenceCount = 0;
            bool isPrivate = false;
            bool alive = false;
        };

        RenderScenes::ModelMaterialTableHandle AddTable(std::span<const u32> materials, bool isPrivate);
        bool Matches(RenderScenes::ModelMaterialTableHandle table, std::span<const u32> materials) const;
        Table* GetTable(RenderScenes::ModelMaterialTableHandle table);
        const Table* GetTable(RenderScenes::ModelMaterialTableHandle table) const;
        void EnsureEntryCapacity(u32 count);

        Renderer::GPUVector<u32> _entries;
        RenderScenes::StableRangeAllocator _entryAllocator;
        std::vector<Table> _tables;
        std::vector<u32> _freeTableIndices;
        robin_hood::unordered_map<u64, std::vector<RenderScenes::ModelMaterialTableHandle>> _sharedTables;
        u32 _sharedTableCount = 0;
        u32 _privateTableCount = 0;
        u32 _liveEntryCount = 0;
    };
} // namespace ModelScene
