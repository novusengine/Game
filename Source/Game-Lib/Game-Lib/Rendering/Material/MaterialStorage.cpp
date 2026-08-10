#include "MaterialStorage.h"

#include <FileFormat/Novus/Model/MaterialABI.h>

#include <Renderer/Renderer.h>
#include <xxhash/xxhash64.h>

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

    u64 HashInstance(const MaterialLoading::MaterialInstanceGPURecord& record, std::span<const u32> textureIndices, std::span<const u32> samplerIDs)
    {
        u64 hash = XXHash64::hash(&record, sizeof(record), 0);
        hash = XXHash64::hash(textureIndices.data(), textureIndices.size_bytes(), hash);
        return XXHash64::hash(samplerIDs.data(), samplerIDs.size_bytes(), hash);
    }
}

namespace MaterialLoading
{
    MaterialStorage::MaterialStorage(bool validateTransfers)
        : _materials(validateTransfers), _materialInstances(validateTransfers), _materialTable(validateTransfers),
          _textureIndices(validateTransfers), _samplerIDs(validateTransfers), _parameterStorage(validateTransfers)
    {
        Configure(_materials, "Material Records");
        Configure(_materialInstances, "Material Instance Records");
        Configure(_materialTable, "Model Default Material Table");
        Configure(_textureIndices, "Material Texture Indices");
        Configure(_samplerIDs, "Material Sampler IDs");
    }

    bool MaterialStorage::InitializeFallback(u32 checkerboardTextureIndex)
    {
        if (_materials.Count() != 0 || _materialInstances.Count() != 0)
            return false;

        std::array<u8, FileFormat::Material::ABI::ParameterLayout::BLOCK_SIZE> parameters = {};
        constexpr std::array<f32, 4> BASE_COLOR = {1.0f, 1.0f, 1.0f, 1.0f};
        constexpr f32 ALPHA_CUTOFF = 0.5f;
        std::memcpy(parameters.data(), BASE_COLOR.data(), sizeof(BASE_COLOR));
        std::memcpy(parameters.data() + FileFormat::Material::ABI::ParameterLayout::ALPHA_CUTOFF_OFFSET, &ALPHA_CUTOFF, sizeof(ALPHA_CUTOFF));
        u32 parameterOffset = 0;
        if (!_parameterStorage.Append(parameters, 16, parameterOffset))
            return false;

        MaterialGPURecord material;
        material.defaultParameterOffset = parameterOffset;
        material.parameterBlockSize = static_cast<u32>(parameters.size());
        material.programID = FALLBACK_MATERIAL_PROGRAM_ID;
        material.flags = FileFormat::Material::MaterialFlags_None;
        MaterialRouting routing;
        _fallbackMaterial = RenderAssets::MaterialHandle(_materials.Add(material));
        _parameterAlignments.push_back(16);
        _materialRouting.push_back(routing);

        MaterialInstanceGPURecord instance;
        instance.parameterOffset = parameterOffset;
        instance.materialIndex = static_cast<RenderAssets::MaterialHandle::type>(_fallbackMaterial);
        instance.flags = FileFormat::Material::MaterialInstanceFlags_TwoSided |
                         FileFormat::Material::MaterialInstanceFlags_CastsShadows;
        instance.packedClassification = 1u;
        instance.materialHandle = static_cast<RenderAssets::MaterialHandle::type>(_fallbackMaterial);
        const std::array<u32, 1> textures = {checkerboardTextureIndex};
        const std::array<u32, 1> samplers = {0};
        if (!AppendInstanceRecord(instance, textures, samplers, _fallbackMaterialInstance))
            return false;
        return _fallbackMaterial != RenderAssets::MaterialHandle::Invalid() && _fallbackMaterialInstance != RenderAssets::MaterialInstanceHandle::Invalid();
    }

    bool MaterialStorage::AppendMaterial(const MaterialAssetView& view, const FileFormat::Material::MaterialProgramRecord& program, RenderAssets::MaterialHandle& outHandle)
    {
        u32 parameterOffset = 0;
        if (!_parameterStorage.Append(view.defaultParameterData, view.root.parameterBlockAlignment, parameterOffset))
            return false;

        MaterialGPURecord record;
        record.defaultParameterOffset = parameterOffset;
        record.parameterBlockSize = view.root.parameterBlockSize;
        record.programID = program.programID;
        record.flags = view.root.flags;
        if (_materials.Count() >= INVALID_MATERIAL_HANDLE)
            return false;
        const u16 baseExecutionGroupID = program.rasterRoutes[0].executionGroupID;
        const auto baseGroupClass = FileFormat::Material::ABI::GetExecutionGroupClass(baseExecutionGroupID);
        if (baseGroupClass != FileFormat::Material::ABI::ExecutionGroup::OpaqueSimple && baseGroupClass != FileFormat::Material::ABI::ExecutionGroup::OpaqueLayered)
            return false;
        MaterialRouting routing;
        routing.baseExecutionGroupID = baseExecutionGroupID;
        for (u32 rasterIndex = 0; rasterIndex < program.rasterRoutes.size(); ++rasterIndex)
        {
            const FileFormat::Material::MaterialProgramRoute& route = program.rasterRoutes[rasterIndex];
            if (route.executionGroupID != baseExecutionGroupID + rasterIndex * 2u || route.groupLocalProgramID == INVALID_GROUP_LOCAL_PROGRAM_ID)
                return false;
        }
        record.packedProgramRouting01 = static_cast<u32>(program.rasterRoutes[0].groupLocalProgramID) | (static_cast<u32>(program.rasterRoutes[1].groupLocalProgramID) << 16u);
        record.packedProgramRouting2AndBaseGroup = static_cast<u32>(program.rasterRoutes[2].groupLocalProgramID) | (static_cast<u32>(baseExecutionGroupID) << 16u);
        outHandle = RenderAssets::MaterialHandle(_materials.Add(record));
        _parameterAlignments.push_back(view.root.parameterBlockAlignment);
        _materialRouting.push_back(routing);
        return outHandle != RenderAssets::MaterialHandle::Invalid();
    }

    bool MaterialStorage::AppendMaterialInstance(RenderAssets::MaterialHandle material, const FileFormat::Material::MaterialInstanceAsset& configuration, std::span<const u8> parameterData,
                                                 std::span<const u32> textureIndices, std::span<const u32> samplerIDs, RenderAssets::MaterialInstanceHandle& outHandle)
    {
        const RenderAssets::MaterialHandle::type materialIndex = static_cast<RenderAssets::MaterialHandle::type>(material);
        if (material == RenderAssets::MaterialHandle::Invalid() || materialIndex >= _materials.Count() || materialIndex >= _parameterAlignments.size())
            return false;

        const MaterialGPURecord& materialRecord = _materials[materialIndex];
        if (parameterData.size() != materialRecord.parameterBlockSize || textureIndices.size() != samplerIDs.size())
            return false;
        const u32 rasterIndex = static_cast<u32>(configuration.rasterClass);
        if (rasterIndex >= 3u || materialIndex >= _materialRouting.size())
            return false;
        const MaterialRouting& routing = _materialRouting[materialIndex];

        u32 parameterOffset = 0;
        if (!_parameterStorage.Append(parameterData, _parameterAlignments[materialIndex], parameterOffset))
            return false;

        MaterialInstanceGPURecord record;
        record.parameterOffset = parameterOffset;
        record.materialIndex = materialIndex;
        record.flags = configuration.flags;
        record.packedClassification = static_cast<u32>(configuration.lightingModelID) |
            (static_cast<u32>(routing.baseExecutionGroupID + rasterIndex * 2u) << 16u);
        record.materialHandle = materialIndex;
        return AppendInstanceRecord(record, textureIndices, samplerIDs, outHandle);
    }

    bool MaterialStorage::DeriveMaterialInstance(RenderAssets::MaterialInstanceHandle base, std::span<const MaterialTextureOverride> overrides, RenderAssets::MaterialInstanceHandle& outHandle)
    {
        const u32 baseIndex = static_cast<RenderAssets::MaterialInstanceHandle::type>(base);
        if (base == RenderAssets::MaterialInstanceHandle::Invalid() || baseIndex >= _materialInstances.Count())
            return false;
        MaterialInstanceGPURecord record = _materialInstances[baseIndex];
        std::vector<u32> textures(record.textureCount);
        std::vector<u32> samplers(record.textureCount);
        for (u32 index = 0; index < record.textureCount; ++index)
        {
            textures[index] = _textureIndices[record.textureOffset + index];
            samplers[index] = _samplerIDs[record.samplerOffset + index];
        }
        for (const MaterialTextureOverride& overrideValue : overrides)
        {
            if (overrideValue.textureSlot >= record.textureCount)
                return false;
            textures[overrideValue.textureSlot] = overrideValue.textureIndex;
            if (overrideValue.overrideSampler)
                samplers[overrideValue.textureSlot] = overrideValue.samplerID;
        }
        record.textureOffset = 0;
        record.samplerOffset = 0;
        return AppendInstanceRecord(record, textures, samplers, outHandle);
    }

    bool MaterialStorage::AppendInstanceRecord(MaterialInstanceGPURecord record, std::span<const u32> textureIndices, std::span<const u32> samplerIDs, RenderAssets::MaterialInstanceHandle& outHandle)
    {
        if (textureIndices.size() != samplerIDs.size() || textureIndices.size() > std::numeric_limits<u32>::max())
            return false;
        record.textureCount = static_cast<u32>(textureIndices.size());
        const u64 hash = HashInstance(record, textureIndices, samplerIDs);
        const auto existing = _instanceKeyToHandles.find(hash);
        if (existing != _instanceKeyToHandles.end())
        {
            for (const RenderAssets::MaterialInstanceHandle handle : existing->second)
            {
                const MaterialInstanceGPURecord& candidate = _materialInstances[static_cast<u32>(handle)];
                if (candidate.parameterOffset != record.parameterOffset || candidate.materialIndex != record.materialIndex || candidate.textureCount != record.textureCount || candidate.flags != record.flags ||
                    candidate.packedClassification != record.packedClassification || candidate.materialHandle != record.materialHandle)
                    continue;
                bool matches = true;
                for (u32 index = 0; index < record.textureCount && matches; ++index)
                    matches = _textureIndices[candidate.textureOffset + index] == textureIndices[index] && _samplerIDs[candidate.samplerOffset + index] == samplerIDs[index];
                if (matches)
                {
                    outHandle = handle;
                    ++_instanceDedupHits;
                    return true;
                }
            }
        }
        record.textureOffset = _textureIndices.AddCount(record.textureCount);
        record.samplerOffset = _samplerIDs.AddCount(record.textureCount);
        for (u32 index = 0; index < record.textureCount; ++index)
        {
            _textureIndices[record.textureOffset + index] = textureIndices[index];
            _samplerIDs[record.samplerOffset + index] = samplerIDs[index];
        }
        outHandle = RenderAssets::MaterialInstanceHandle(_materialInstances.Add(record));
        _instanceKeyToHandles[hash].push_back(outHandle);
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
        _bufferGrowths += _textureIndices.SyncToGPU(renderer) ? 1u : 0u;
        _bufferGrowths += _samplerIDs.SyncToGPU(renderer) ? 1u : 0u;
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
        stats.usedBytes = _materials.UsedBytes() + _materialInstances.UsedBytes() + _materialTable.UsedBytes() +
            _textureIndices.UsedBytes() + _samplerIDs.UsedBytes();
        stats.reservedBytes = _materials.TotalBytes() + _materialInstances.TotalBytes() + _materialTable.TotalBytes() +
            _textureIndices.TotalBytes() + _samplerIDs.TotalBytes();
        stats.parameters = _parameterStorage.GetStats();
        stats.usedBytes += stats.parameters.usedBytes;
        stats.reservedBytes += stats.parameters.reservedBytes;
        return stats;
    }
} // namespace MaterialLoading
