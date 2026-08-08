#include "ModelMaterialTableStore.h"

#include <Renderer/Renderer.h>

#include <tracy/Tracy.hpp>
#include <xxhash/xxhash64.h>

namespace ModelScene
{
    ModelMaterialTableStore::ModelMaterialTableStore(bool validateTransfers)
        : _entries(validateTransfers)
    {
        _entries.SetDebugName("Scene Model Material Tables");
        _entries.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    void ModelMaterialTableStore::Reserve(u32 tableCount, u32 entryCount)
    {
        if (entryCount > _entries.Count())
            _entries.Reserve(entryCount - _entries.Count());
        _tables.reserve(std::max(_tables.size(), static_cast<size_t>(tableCount)));
        _sharedTables.reserve(_sharedTables.size() + tableCount);
    }

    RenderScenes::ModelMaterialTableHandle ModelMaterialTableStore::AcquireShared(std::span<const u32> materials)
    {
        ZoneScopedN("ModelMaterialTableStore::AcquireShared");

        const u64 hash = XXHash64::hash(materials.data(), materials.size_bytes(), materials.size());
        const auto existing = _sharedTables.find(hash);
        if (existing != _sharedTables.end())
        {
            for (const RenderScenes::ModelMaterialTableHandle candidate : existing->second)
            {
                if (!Matches(candidate, materials))
                    continue;

                GetTable(candidate)->referenceCount++;
                return candidate;
            }
        }

        const RenderScenes::ModelMaterialTableHandle handle = AddTable(materials, false);
        if (IsValid(handle))
        {
            _sharedTables[hash].push_back(handle);
            _sharedTableCount++;
        }
        return handle;
    }

    RenderScenes::ModelMaterialTableHandle ModelMaterialTableStore::CreatePrivate(RenderScenes::ModelMaterialTableHandle source)
    {
        const Table* sourceTable = GetTable(source);
        if (!sourceTable)
            return RenderScenes::InvalidModelMaterialTableHandle();

        std::vector<u32> materials(sourceTable->range.count);
        for (u32 index = 0; index < sourceTable->range.count; ++index)
            materials[index] = _entries[sourceTable->range.offset + index];

        const RenderScenes::ModelMaterialTableHandle handle = AddTable(materials, true);
        if (IsValid(handle))
            _privateTableCount++;
        return handle;
    }

    bool ModelMaterialTableStore::SetMaterial(RenderScenes::ModelMaterialTableHandle table, u32 slot,
                                               RenderAssets::MaterialInstanceHandle material)
    {
        Table* record = GetTable(table);
        if (!record || !record->isPrivate || slot >= record->range.count)
            return false;

        _entries[record->range.offset + slot] = static_cast<RenderAssets::MaterialInstanceHandle::type>(material);
        _entries.SetDirtyElement(record->range.offset + slot);
        return true;
    }

    void ModelMaterialTableStore::Release(RenderScenes::ModelMaterialTableHandle table)
    {
        Table* record = GetTable(table);
        if (!record || record->referenceCount == 0)
            return;

        record->referenceCount--;
        if (record->referenceCount != 0 || !record->isPrivate)
            return;

        _entryAllocator.Free(record->range);
        _liveEntryCount -= record->range.count;
        record->alive = false;
        record->range = {};
        _privateTableCount--;
        _freeTableIndices.push_back(static_cast<u32>(static_cast<RenderScenes::ModelMaterialTableHandle::type>(table)));
    }

    void ModelMaterialTableStore::SyncToGPU(Renderer::Renderer* renderer)
    {
        _entries.SyncToGPU(renderer);
    }

    bool ModelMaterialTableStore::IsValid(RenderScenes::ModelMaterialTableHandle table) const
    {
        return GetTable(table) != nullptr;
    }

    bool ModelMaterialTableStore::IsPrivate(RenderScenes::ModelMaterialTableHandle table) const
    {
        const Table* record = GetTable(table);
        return record && record->isPrivate;
    }

    u32 ModelMaterialTableStore::GetOffset(RenderScenes::ModelMaterialTableHandle table) const
    {
        const Table* record = GetTable(table);
        return record ? record->range.offset : 0;
    }

    u32 ModelMaterialTableStore::GetCount(RenderScenes::ModelMaterialTableHandle table) const
    {
        const Table* record = GetTable(table);
        return record ? record->range.count : 0;
    }

    u32 ModelMaterialTableStore::GetMaterial(RenderScenes::ModelMaterialTableHandle table, u32 slot) const
    {
        const Table* record = GetTable(table);
        if (!record || slot >= record->range.count)
            return RenderScenes::INVALID_SCENE_INDEX;
        return _entries[record->range.offset + slot];
    }

    ModelMaterialTableStoreStats ModelMaterialTableStore::GetStats() const
    {
        ModelMaterialTableStoreStats stats;
        stats.sharedTables = _sharedTableCount;
        stats.privateTables = _privateTableCount;
        stats.liveEntries = _liveEntryCount;
        stats.allocatedEntries = _entryAllocator.GetAddressSpaceSize();
        stats.freeEntries = _entryAllocator.GetFreeCount();
        return stats;
    }

    RenderScenes::ModelMaterialTableHandle ModelMaterialTableStore::AddTable(std::span<const u32> materials, bool isPrivate)
    {
        const RenderScenes::StableRange range = _entryAllocator.Allocate(static_cast<u32>(materials.size()));
        const u32 previousEntryCount = _entries.Count();
        if (range.offset + range.count > previousEntryCount)
            _entries.Reserve(range.offset + range.count - previousEntryCount);
        EnsureEntryCapacity(range.offset + range.count);
        for (u32 index = 0; index < range.count; ++index)
            _entries[range.offset + index] = materials[index];
        if (range.count != 0 && range.offset < previousEntryCount)
            _entries.SetDirtyElements(range.offset, range.count);

        u32 tableIndex = 0;
        if (_freeTableIndices.empty())
        {
            tableIndex = static_cast<u32>(_tables.size());
            _tables.emplace_back();
        }
        else
        {
            tableIndex = _freeTableIndices.back();
            _freeTableIndices.pop_back();
        }

        _tables[tableIndex] = { range, 1, isPrivate, true };
        _liveEntryCount += range.count;
        return RenderScenes::ModelMaterialTableHandle(tableIndex);
    }

    bool ModelMaterialTableStore::Matches(RenderScenes::ModelMaterialTableHandle table,
                                          std::span<const u32> materials) const
    {
        const Table* record = GetTable(table);
        if (!record || record->range.count != materials.size())
            return false;

        for (u32 index = 0; index < record->range.count; ++index)
        {
            if (_entries[record->range.offset + index] != materials[index])
                return false;
        }
        return true;
    }

    ModelMaterialTableStore::Table* ModelMaterialTableStore::GetTable(RenderScenes::ModelMaterialTableHandle table)
    {
        const u32 index = static_cast<RenderScenes::ModelMaterialTableHandle::type>(table);
        return index < _tables.size() && _tables[index].alive ? &_tables[index] : nullptr;
    }

    const ModelMaterialTableStore::Table* ModelMaterialTableStore::GetTable(RenderScenes::ModelMaterialTableHandle table) const
    {
        const u32 index = static_cast<RenderScenes::ModelMaterialTableHandle::type>(table);
        return index < _tables.size() && _tables[index].alive ? &_tables[index] : nullptr;
    }

    void ModelMaterialTableStore::EnsureEntryCapacity(u32 count)
    {
        if (_entries.Count() < count)
            _entries.AddCount(count - _entries.Count());
    }
} // namespace ModelScene
