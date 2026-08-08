#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "ModelAssetReader.h"

#include <Renderer/GPUVector.h>

#include <span>
#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace Map
{
    struct ModelResourceAllocationHints;
}

namespace ModelLoading
{
    struct ModelGPURecord
    {
        FileFormat::Model::Bounds bounds;
        FileFormat::AssetID collisionAssetID = FileFormat::INVALID_ASSET_ID;

        u32 meshBase = 0;
        u32 numMeshes = 0;
        u32 meshLODBase = 0;
        u32 numMeshLODs = 0;
        u32 submeshBase = 0;
        u32 numSubmeshes = 0;
        u32 meshletBase = 0;
        u32 numMeshlets = 0;
        u32 positionBase = 0;
        u32 numPositions = 0;
        u32 vertexAttributeBase = 0;
        u32 numVertexAttributes = 0;
        u32 skinningDataBase = 0;
        u32 numSkinningData = 0;
        u32 meshletVertexIndexBase = 0;
        u32 numMeshletVertexIndices = 0;
        u32 meshletTriangleBase = 0;
        u32 numMeshletTriangles = 0;
        u32 jointPaletteRemapBase = 0;
        u32 numJointPaletteRemaps = 0;
        u32 materialSlotBase = 0;
        u32 numMaterialSlots = 0;
        u32 embeddedInstanceSetBase = 0;
        u32 numEmbeddedInstanceSets = 0;
        u32 embeddedInstanceBase = 0;
        u32 numEmbeddedInstances = 0;

        u32 defaultMaterialTableOffset = 0;
        u32 defaultMaterialTableCount = 0;
        u32 flags = 0;
        u32 geometryGroupCount = 0;
    };

    struct ModelGeometryStorageStats
    {
        u32 numModels = 0;
        u32 numMeshes = 0;
        u32 numMeshLODs = 0;
        u32 numSubmeshes = 0;
        u32 numMeshlets = 0;
        u32 numPositions = 0;
        u64 usedBytes = 0;
        u64 reservedBytes = 0;
        u32 bufferGrowths = 0;
    };

    // Owns CPU-side staging and GPU-side buffers for model records, geometry, skinning data, and embedded instances.
    // Stable handles and base indices allow incremental uploads without relocating live model data.
    class ModelGeometryStorage
    {
      public:
        explicit ModelGeometryStorage(bool validateTransfers = false);

        void Reserve(const Map::ModelResourceAllocationHints& hints);
        bool Append(const ModelAssetView& view, u32 materialTableOffset, u32 materialTableCount, RenderAssets::ModelHandle& outHandle);
        void SyncToGPU(Renderer::Renderer* renderer);

        const ModelGPURecord& GetRecord(RenderAssets::ModelHandle handle) const
        {
            return _records[static_cast<RenderAssets::ModelHandle::type>(handle)];
        }
        bool HasModel(RenderAssets::ModelHandle handle) const
        {
            return static_cast<RenderAssets::ModelHandle::type>(handle) < _records.Count();
        }
        bool FindMaterialSlot(RenderAssets::ModelHandle handle, u32 stableID, u32& outSlot) const;
        std::span<const FileFormat::Model::Parameter> GetParameters(RenderAssets::ModelHandle handle) const;
        std::span<const FileFormat::Model::ParameterBinding> GetParameterBindings(RenderAssets::ModelHandle handle) const;
        ModelGeometryStorageStats GetStats() const;

        const Renderer::GPUVector<ModelGPURecord>& GetRecords() const { return _records; }
        const Renderer::GPUVector<FileFormat::Model::Mesh>& GetMeshes() const { return _meshes; }
        const Renderer::GPUVector<FileFormat::Model::MeshLOD>& GetMeshLODs() const { return _meshLODs; }
        const Renderer::GPUVector<FileFormat::Model::Submesh>& GetSubmeshes() const { return _submeshes; }
        const Renderer::GPUVector<FileFormat::Model::Meshlet>& GetMeshlets() const { return _meshlets; }
        const Renderer::GPUVector<FileFormat::Model::PackedPosition>& GetPositions() const { return _positions; }
        const Renderer::GPUVector<FileFormat::Model::PackedVertexAttributes>& GetVertexAttributes() const { return _vertexAttributes; }
        const Renderer::GPUVector<FileFormat::Model::PackedSkinningData>& GetSkinningData() const { return _skinningData; }
        const Renderer::GPUVector<u32>& GetMeshletVertexIndices() const { return _meshletVertexIndices; }
        const Renderer::GPUVector<FileFormat::Model::PackedMeshletTriangle>& GetMeshletTriangles() const { return _meshletTriangles; }
        const Renderer::GPUVector<u16>& GetJointPaletteRemaps() const { return _jointPaletteRemaps; }
        const Renderer::GPUVector<FileFormat::Model::MaterialSlot>& GetMaterialSlots() const { return _materialSlots; }
        const Renderer::GPUVector<FileFormat::Model::EmbeddedInstanceSet>& GetEmbeddedInstanceSets() const { return _embeddedInstanceSets; }
        const Renderer::GPUVector<FileFormat::Model::EmbeddedInstance>& GetEmbeddedInstances() const { return _embeddedInstances; }

      private:
        struct ParameterRange
        {
            u32 parameterOffset = 0;
            u32 parameterCount = 0;
            u32 bindingOffset = 0;
            u32 bindingCount = 0;
        };

        Renderer::GPUVector<ModelGPURecord> _records;
        Renderer::GPUVector<FileFormat::Model::Mesh> _meshes;
        Renderer::GPUVector<FileFormat::Model::MeshLOD> _meshLODs;
        Renderer::GPUVector<FileFormat::Model::Submesh> _submeshes;
        Renderer::GPUVector<FileFormat::Model::Meshlet> _meshlets;
        Renderer::GPUVector<FileFormat::Model::PackedPosition> _positions;
        Renderer::GPUVector<FileFormat::Model::PackedVertexAttributes> _vertexAttributes;
        Renderer::GPUVector<FileFormat::Model::PackedSkinningData> _skinningData;
        Renderer::GPUVector<u32> _meshletVertexIndices;
        Renderer::GPUVector<FileFormat::Model::PackedMeshletTriangle> _meshletTriangles;
        Renderer::GPUVector<u16> _jointPaletteRemaps;
        Renderer::GPUVector<FileFormat::Model::MaterialSlot> _materialSlots;
        Renderer::GPUVector<FileFormat::Model::EmbeddedInstanceSet> _embeddedInstanceSets;
        Renderer::GPUVector<FileFormat::Model::EmbeddedInstance> _embeddedInstances;
        std::vector<ParameterRange> _parameterRanges;
        std::vector<FileFormat::Model::Parameter> _parameters;
        std::vector<FileFormat::Model::ParameterBinding> _parameterBindings;
        u32 _bufferGrowths = 0;
    };
} // namespace ModelLoading
