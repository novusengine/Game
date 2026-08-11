#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "MaterialAssetReader.h"
#include "MaterialParameterStorage.h"

#include <FileFormat/Novus/Model/MaterialABI.h>
#include <FileFormat/Novus/Model/MaterialPack.h>
#include <Renderer/GPUVector.h>

#include <robinhood/robinhood.h>

#include <array>
#include <cstddef>
#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace MaterialLoading
{
    inline constexpr u32 FALLBACK_MATERIAL_PROGRAM_ID = 0xFFFFFFFFu;
    inline constexpr u16 INVALID_MATERIAL_HANDLE = 0xFFFFu;
    inline constexpr u16 INVALID_GROUP_LOCAL_PROGRAM_ID = 0xFFFFu;

    struct MaterialGPURecord
    {
        u32 defaultParameterOffset = 0;
        u32 parameterBlockSize = 0;
        u32 programID = 0;
        u32 flags = 0;
        u32 packedProgramRouting01 = 0;
        u32 packedProgramRouting2AndBaseGroup = 0;
    };

    struct MaterialInstanceGPURecord
    {
        u32 parameterOffset = 0;
        u32 materialIndex = 0;
        u32 textureOffset = 0;
        u32 textureCount = 0;
        u32 flags = 0;
        u32 packedClassification = 0;
        u32 materialHandle = 0;
        u32 samplerOffset = 0;
    };
    static_assert(sizeof(MaterialGPURecord) == 24);
    static_assert(sizeof(MaterialInstanceGPURecord) == 32);

    struct MaterialTextureOverride
    {
        u32 textureSlot = 0;
        u32 textureIndex = 0;
        u32 samplerID = 0;
        bool overrideSampler = false;
    };

    struct MaterialStorageStats
    {
        u32 numMaterials = 0;
        u32 numMaterialInstances = 0;
        u32 numMaterialTableEntries = 0;
        u32 instanceDedupHits = 0;
        u32 bufferGrowths = 0;
        u64 usedBytes = 0;
        u64 reservedBytes = 0;
        MaterialParameterStorageStats parameters;
    };

    // Owns CPU-side staging and GPU-side buffers for materials, material instances, parameter blocks, and material tables.
    // Its stable handles allow incremental uploads without invalidating live Scene material references.
    class MaterialStorage
    {
      public:
        explicit MaterialStorage(bool validateTransfers = false);

        bool InitializeFallback(u32 checkerboardTextureIndex);
        bool AppendMaterial(const MaterialAssetView& view, const FileFormat::Material::MaterialProgramRecord& program, RenderAssets::MaterialHandle& outHandle);
        bool AppendMaterialInstance(RenderAssets::MaterialHandle material, const FileFormat::Material::MaterialInstanceAsset& configuration, std::span<const u8> parameterData,
                                    std::span<const u32> textureIndices, std::span<const u32> samplerIDs, RenderAssets::MaterialInstanceHandle& outHandle,
                                    bool mutableParameters = false);
        bool WriteMaterialParameters(RenderAssets::MaterialInstanceHandle materialInstance, u32 relativeOffset, std::span<const u8> bytes);
        bool DeriveMaterialInstance(RenderAssets::MaterialInstanceHandle base, std::span<const MaterialTextureOverride> overrides, RenderAssets::MaterialInstanceHandle& outHandle);
        bool AppendMaterialTable(std::span<const RenderAssets::MaterialInstanceHandle> materials, u32& outOffset);
        void SyncToGPU(Renderer::Renderer* renderer);

        RenderAssets::MaterialHandle GetFallbackMaterial() const { return _fallbackMaterial; }
        RenderAssets::MaterialInstanceHandle GetFallbackMaterialInstance() const { return _fallbackMaterialInstance; }
        const MaterialGPURecord& GetMaterial(RenderAssets::MaterialHandle handle) const
        {
            return _materials[static_cast<RenderAssets::MaterialHandle::type>(handle)];
        }
        const MaterialInstanceGPURecord& GetMaterialInstance(RenderAssets::MaterialInstanceHandle handle) const
        {
            return _materialInstances[static_cast<RenderAssets::MaterialInstanceHandle::type>(handle)];
        }
        bool HasMaterial(RenderAssets::MaterialHandle handle) const
        {
            return static_cast<RenderAssets::MaterialHandle::type>(handle) < _materials.Count();
        }
        bool HasMaterialInstance(RenderAssets::MaterialInstanceHandle handle) const
        {
            return static_cast<RenderAssets::MaterialInstanceHandle::type>(handle) < _materialInstances.Count();
        }
        u32 GetMaterialTableEntry(u32 offset) const { return _materialTable[offset]; }
        MaterialStorageStats GetStats() const;

        const Renderer::GPUVector<MaterialGPURecord>& GetMaterials() const { return _materials; }
        const Renderer::GPUVector<MaterialInstanceGPURecord>& GetMaterialInstances() const { return _materialInstances; }
        const Renderer::GPUVector<u32>& GetMaterialTable() const { return _materialTable; }
        const Renderer::GPUVector<u32>& GetTextureIndices() const { return _textureIndices; }
        const Renderer::GPUVector<u32>& GetSamplerIDs() const { return _samplerIDs; }
        const MaterialParameterStorage& GetParameterStorage() const { return _parameterStorage; }

      private:
        struct MaterialRouting
        {
            u16 baseExecutionGroupID = 0;
        };

        Renderer::GPUVector<MaterialGPURecord> _materials;
        Renderer::GPUVector<MaterialInstanceGPURecord> _materialInstances;
        Renderer::GPUVector<u32> _materialTable;
        Renderer::GPUVector<u32> _textureIndices;
        Renderer::GPUVector<u32> _samplerIDs;
        MaterialParameterStorage _parameterStorage;
        std::vector<u32> _parameterAlignments;
        std::vector<MaterialRouting> _materialRouting;
        robin_hood::unordered_map<u64, std::vector<RenderAssets::MaterialInstanceHandle>> _instanceKeyToHandles;
        RenderAssets::MaterialHandle _fallbackMaterial;
        RenderAssets::MaterialInstanceHandle _fallbackMaterialInstance;
        u32 _instanceDedupHits = 0;
        u32 _bufferGrowths = 0;

        bool AppendInstanceRecord(MaterialInstanceGPURecord record, std::span<const u32> textureIndices, std::span<const u32> samplerIDs, RenderAssets::MaterialInstanceHandle& outHandle);
    };
} // namespace MaterialLoading
