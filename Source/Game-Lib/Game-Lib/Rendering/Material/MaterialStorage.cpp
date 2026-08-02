#include "MaterialStorage.h"

#include <Renderer/Renderer.h>

#include <array>
#include <cstring>
#include <limits>

namespace
{
    template <typename T>
    void Configure(Renderer::GPUVector<T>& buffer, const char* name)
    {
        buffer.SetDebugName(name);
        buffer.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }
}

namespace MaterialLoading
{
    MaterialStorage::MaterialStorage(bool validateTransfers)
        : _materials(validateTransfers), _materialInstances(validateTransfers), _materialTable(validateTransfers), _parameterStorage(validateTransfers)
    {
        Configure(_materials, "Material Records");
        Configure(_materialInstances, "Material Instance Records");
        Configure(_materialTable, "Model Default Material Table");
    }

    bool MaterialStorage::InitializeFallback(u32 checkerboardTextureIndex)
    {
        if (_materials.Count() != 0 || _materialInstances.Count() != 0)
            return false;

        std::array<u8, 16> parameters = {};
        std::memcpy(parameters.data(), &checkerboardTextureIndex, sizeof(checkerboardTextureIndex));
        u32 parameterOffset = 0;
        if (!_parameterStorage.Append(parameters, 16, parameterOffset))
            return false;

        MaterialGPURecord material;
        material.defaultParameterOffset = parameterOffset;
        material.parameterBlockSize = static_cast<u32>(parameters.size());
        material.programID = FALLBACK_MATERIAL_PROGRAM_ID;
        material.flags = FileFormat::Material::MaterialFlags_TwoSided;
        material.lightingModelID = 1;
        material.materialExecutionGroupID = 0;
        material.rasterClass = static_cast<u8>(FileFormat::Material::RasterClass::Solid);
        _fallbackMaterial = RenderAssets::MaterialHandle(_materials.Add(material));
        _parameterAlignments.push_back(16);

        MaterialInstanceGPURecord instance;
        instance.parameterOffset = parameterOffset;
        instance.materialIndex = static_cast<RenderAssets::MaterialHandle::type>(_fallbackMaterial);
        _fallbackMaterialInstance = RenderAssets::MaterialInstanceHandle(_materialInstances.Add(instance));
        _instanceKeyToHandle[(static_cast<u64>(instance.materialIndex) << 32u) | instance.parameterOffset] = _fallbackMaterialInstance;
        return _fallbackMaterial != RenderAssets::MaterialHandle::Invalid() &&
               _fallbackMaterialInstance != RenderAssets::MaterialInstanceHandle::Invalid();
    }

    bool MaterialStorage::AppendMaterial(const MaterialAssetView& view, RenderAssets::MaterialHandle& outHandle)
    {
        u32 parameterOffset = 0;
        if (!_parameterStorage.Append(view.defaultParameterData, view.root.parameterBlockAlignment, parameterOffset))
            return false;

        MaterialGPURecord record;
        record.defaultParameterOffset = parameterOffset;
        record.parameterBlockSize = view.root.parameterBlockSize;
        record.programID = view.root.programID;
        record.flags = view.root.flags;
        record.lightingModelID = view.root.lightingModelID;
        record.materialExecutionGroupID = view.root.materialExecutionGroupID;
        record.rasterClass = static_cast<u8>(view.root.rasterClass);
        outHandle = RenderAssets::MaterialHandle(_materials.Add(record));
        _parameterAlignments.push_back(view.root.parameterBlockAlignment);
        return outHandle != RenderAssets::MaterialHandle::Invalid();
    }

    bool MaterialStorage::AppendMaterialInstance(RenderAssets::MaterialHandle material, std::span<const u8> patchedParameterData,
                                                 RenderAssets::MaterialInstanceHandle& outHandle)
    {
        const RenderAssets::MaterialHandle::type materialIndex = static_cast<RenderAssets::MaterialHandle::type>(material);
        if (material == RenderAssets::MaterialHandle::Invalid() || materialIndex >= _materials.Count() || materialIndex >= _parameterAlignments.size())
            return false;

        const MaterialGPURecord& materialRecord = _materials[materialIndex];
        if (patchedParameterData.size() != materialRecord.parameterBlockSize)
            return false;

        u32 parameterOffset = 0;
        if (!_parameterStorage.Append(patchedParameterData, _parameterAlignments[materialIndex], parameterOffset))
            return false;

        const u64 key = (static_cast<u64>(materialIndex) << 32u) | parameterOffset;
        const auto existing = _instanceKeyToHandle.find(key);
        if (existing != _instanceKeyToHandle.end())
        {
            outHandle = existing->second;
            ++_instanceDedupHits;
            return true;
        }

        MaterialInstanceGPURecord record;
        record.parameterOffset = parameterOffset;
        record.materialIndex = materialIndex;
        outHandle = RenderAssets::MaterialInstanceHandle(_materialInstances.Add(record));
        _instanceKeyToHandle[key] = outHandle;
        return outHandle != RenderAssets::MaterialInstanceHandle::Invalid();
    }

    bool MaterialStorage::AppendMaterialTable(std::span<const RenderAssets::MaterialInstanceHandle> materials, u32& outOffset)
    {
        if (materials.size() > std::numeric_limits<u32>::max() - _materialTable.Count())
            return false;

        if (materials.empty())
        {
            outOffset = _materialTable.Count();
            return true;
        }

        for (const RenderAssets::MaterialInstanceHandle material : materials)
        {
            if (material == RenderAssets::MaterialInstanceHandle::Invalid())
                return false;
        }

        outOffset = _materialTable.AddCount(static_cast<u32>(materials.size()));
        for (u32 index = 0; index < materials.size(); ++index)
            _materialTable[outOffset + index] = static_cast<RenderAssets::MaterialInstanceHandle::type>(materials[index]);
        return true;
    }

    void MaterialStorage::SyncToGPU(Renderer::Renderer* renderer)
    {
        _bufferGrowths += _materials.SyncToGPU(renderer) ? 1u : 0u;
        _bufferGrowths += _materialInstances.SyncToGPU(renderer) ? 1u : 0u;
        _bufferGrowths += _materialTable.SyncToGPU(renderer) ? 1u : 0u;
        _parameterStorage.SyncToGPU(renderer);
    }

    MaterialStorageStats MaterialStorage::GetStats() const
    {
        MaterialStorageStats stats;
        stats.numMaterials = _materials.Count();
        stats.numMaterialInstances = _materialInstances.Count();
        stats.numMaterialTableEntries = _materialTable.Count();
        stats.instanceDedupHits = _instanceDedupHits;
        stats.bufferGrowths = _bufferGrowths;
        stats.usedBytes = _materials.UsedBytes() + _materialInstances.UsedBytes() + _materialTable.UsedBytes();
        stats.reservedBytes = _materials.TotalBytes() + _materialInstances.TotalBytes() + _materialTable.TotalBytes();
        stats.parameters = _parameterStorage.GetStats();
        stats.usedBytes += stats.parameters.usedBytes;
        stats.reservedBytes += stats.parameters.reservedBytes;
        return stats;
    }
} // namespace MaterialLoading
