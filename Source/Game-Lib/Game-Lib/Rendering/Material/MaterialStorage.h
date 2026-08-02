#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "MaterialAssetReader.h"
#include "MaterialParameterStorage.h"

#include <Renderer/GPUVector.h>

#include <robinhood/robinhood.h>

#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace MaterialLoading
{
    inline constexpr u32 FALLBACK_MATERIAL_PROGRAM_ID = 0xFFFFFFFFu;

    struct MaterialGPURecord
    {
        u32 defaultParameterOffset = 0;
        u32 parameterBlockSize = 0;
        u32 programID = 0;
        u32 flags = 0;
        u16 lightingModelID = 0;
        u16 materialExecutionGroupID = 0;
        u8 rasterClass = 0;
        u8 reserved[3] = {};
    };

    struct MaterialInstanceGPURecord
    {
        u32 parameterOffset = 0;
        u32 materialIndex = 0;
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
        bool AppendMaterial(const MaterialAssetView& view, RenderAssets::MaterialHandle& outHandle);
        bool AppendMaterialInstance(RenderAssets::MaterialHandle material, std::span<const u8> patchedParameterData,
                                    RenderAssets::MaterialInstanceHandle& outHandle);
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
        const MaterialParameterStorage& GetParameterStorage() const { return _parameterStorage; }

      private:
        Renderer::GPUVector<MaterialGPURecord> _materials;
        Renderer::GPUVector<MaterialInstanceGPURecord> _materialInstances;
        Renderer::GPUVector<u32> _materialTable;
        MaterialParameterStorage _parameterStorage;
        std::vector<u32> _parameterAlignments;
        robin_hood::unordered_map<u64, RenderAssets::MaterialInstanceHandle> _instanceKeyToHandle;
        RenderAssets::MaterialHandle _fallbackMaterial;
        RenderAssets::MaterialInstanceHandle _fallbackMaterialInstance;
        u32 _instanceDedupHits = 0;
        u32 _bufferGrowths = 0;
    };
} // namespace MaterialLoading
