#include "ModelGeometryStorage.h"

#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/Map/Map.h>

#include <Renderer/Renderer.h>

#include <cstring>
#include <limits>

namespace
{
    template <typename T>
    bool CanAppendSpan(const Renderer::GPUVector<T>& destination, std::span<const T> source)
    {
        return source.size() <= std::numeric_limits<u32>::max() - destination.Count();
    }

    template <typename T>
    u32 AppendSpan(Renderer::GPUVector<T>& destination, std::span<const T> source)
    {
        if (source.empty())
            return destination.Count();

        const u32 base = destination.AddCountUninitialized(static_cast<u32>(source.size()));
        std::memcpy(&destination[base], source.data(), source.size_bytes());
        return base;
    }

    template <typename T>
    void ReserveSpan(Renderer::GPUVector<T>& destination, std::span<const T> source)
    {
        destination.Reserve(static_cast<u32>(source.size()));
    }

    template <typename T>
    void ReserveHint(Renderer::GPUVector<T>& destination, u64 count, const char* name)
    {
        constexpr u64 MAX_COUNT = std::numeric_limits<u32>::max() / sizeof(T);
        if (count > MAX_COUNT)
        {
            NC_LOG_WARNING("MODEL_ALLOCATION_HINT ignored resource={} count={} max={}", name, count,
                           MAX_COUNT);
            return;
        }

        destination.Reserve(static_cast<u32>(count));
    }

    template <typename T>
    void Configure(Renderer::GPUVector<T>& buffer, const char* name)
    {
        buffer.SetDebugName(name);
        buffer.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }
} // namespace

namespace ModelLoading
{
    ModelGeometryStorage::ModelGeometryStorage(bool validateTransfers)
        : _records(validateTransfers), _meshes(validateTransfers), _meshLODs(validateTransfers), _submeshes(validateTransfers), _meshlets(validateTransfers),
          _positions(validateTransfers), _vertexAttributes(validateTransfers), _skinningData(validateTransfers), _meshletVertexIndices(validateTransfers),
          _meshletTriangles(validateTransfers), _jointPaletteRemaps(validateTransfers), _materialSlots(validateTransfers),
          _embeddedInstanceSets(validateTransfers), _embeddedInstances(validateTransfers)
    {
        Configure(_records, "Model Asset Records");
        Configure(_meshes, "Model Meshes");
        Configure(_meshLODs, "Model Mesh LODs");
        Configure(_submeshes, "Model Submeshes");
        Configure(_meshlets, "Model Meshlets");
        Configure(_positions, "Model Positions");
        Configure(_vertexAttributes, "Model Vertex Attributes");
        Configure(_skinningData, "Model Skinning Data");
        Configure(_meshletVertexIndices, "Model Meshlet Vertex Indices");
        Configure(_meshletTriangles, "Model Meshlet Triangles");
        Configure(_jointPaletteRemaps, "Model Joint Palette Remaps");
        Configure(_materialSlots, "Model Material Slots");
        Configure(_embeddedInstanceSets, "Model Embedded Instance Sets");
        Configure(_embeddedInstances, "Model Embedded Instances");
    }

    void ModelGeometryStorage::Reserve(const Map::ModelResourceAllocationHints& hints)
    {
        ZoneScopedN("ModelGeometryStorage::Reserve");

        ReserveHint(_records, hints.models, "models");
        ReserveHint(_meshes, hints.meshes, "meshes");
        ReserveHint(_meshLODs, hints.meshLODs, "mesh_lods");
        ReserveHint(_submeshes, hints.submeshes, "submeshes");
        ReserveHint(_meshlets, hints.meshlets, "meshlets");
        ReserveHint(_positions, hints.positions, "positions");
        ReserveHint(_vertexAttributes, hints.vertexAttributes, "vertex_attributes");
        ReserveHint(_skinningData, hints.skinningRecords, "skinning_records");
        ReserveHint(_meshletVertexIndices, hints.meshletVertexIndices, "meshlet_vertex_indices");
        ReserveHint(_meshletTriangles, hints.meshletTriangleRecords, "meshlet_triangles");
        ReserveHint(_jointPaletteRemaps, hints.jointPaletteRemaps, "joint_palette_remaps");
        ReserveHint(_materialSlots, hints.materialSlots, "material_slots");
        ReserveHint(_embeddedInstanceSets, hints.embeddedInstanceSets, "embedded_instance_sets");
        ReserveHint(_embeddedInstances, hints.embeddedInstanceRecords, "embedded_instances");
    }

    bool ModelGeometryStorage::Append(const ModelAssetView& view, u32 materialTableOffset, u32 materialTableCount, RenderAssets::ModelHandle& outHandle)
    {
        ZoneScopedN("ModelGeometryStorage::Append");

        if (_records.Count() == std::numeric_limits<u32>::max() || !CanAppendSpan(_meshes, view.meshes) || !CanAppendSpan(_meshLODs, view.meshLODs) ||
            !CanAppendSpan(_submeshes, view.submeshes) || !CanAppendSpan(_meshlets, view.meshlets) || !CanAppendSpan(_positions, view.positions) ||
            !CanAppendSpan(_vertexAttributes, view.vertexAttributes) || !CanAppendSpan(_skinningData, view.skinningData) ||
            !CanAppendSpan(_meshletVertexIndices, view.meshletVertexIndices) || !CanAppendSpan(_meshletTriangles, view.meshletTriangles) ||
            !CanAppendSpan(_jointPaletteRemaps, view.jointPaletteRemaps) || !CanAppendSpan(_materialSlots, view.materialSlots) ||
            !CanAppendSpan(_embeddedInstanceSets, view.embeddedInstanceSets) || !CanAppendSpan(_embeddedInstances, view.embeddedInstances))
            return false;

        _records.Reserve(1);
        ReserveSpan(_meshes, view.meshes);
        ReserveSpan(_meshLODs, view.meshLODs);
        ReserveSpan(_submeshes, view.submeshes);
        ReserveSpan(_meshlets, view.meshlets);
        ReserveSpan(_positions, view.positions);
        ReserveSpan(_vertexAttributes, view.vertexAttributes);
        ReserveSpan(_skinningData, view.skinningData);
        ReserveSpan(_meshletVertexIndices, view.meshletVertexIndices);
        ReserveSpan(_meshletTriangles, view.meshletTriangles);
        ReserveSpan(_jointPaletteRemaps, view.jointPaletteRemaps);
        ReserveSpan(_materialSlots, view.materialSlots);
        ReserveSpan(_embeddedInstanceSets, view.embeddedInstanceSets);
        ReserveSpan(_embeddedInstances, view.embeddedInstances);

        ModelGPURecord record;
        record.bounds = view.root.bounds;
        record.collisionAssetID = view.root.collisionAssetID;
        record.meshBase = AppendSpan(_meshes, view.meshes);
        record.numMeshes = static_cast<u32>(view.meshes.size());
        record.meshLODBase = AppendSpan(_meshLODs, view.meshLODs);
        record.numMeshLODs = static_cast<u32>(view.meshLODs.size());
        record.submeshBase = AppendSpan(_submeshes, view.submeshes);
        record.numSubmeshes = static_cast<u32>(view.submeshes.size());
        record.meshletBase = AppendSpan(_meshlets, view.meshlets);
        record.numMeshlets = static_cast<u32>(view.meshlets.size());
        for (const FileFormat::Model::Mesh& mesh : view.meshes)
        {
            if (mesh.numLODs == 0 || mesh.lodOffset >= view.meshLODs.size())
                continue;
            const FileFormat::Model::MeshLOD& lod = view.meshLODs[mesh.lodOffset];
            record.lod0Meshlets += lod.numMeshlets;
            for (u32 meshletIndex = 0; meshletIndex < lod.numMeshlets; ++meshletIndex)
            {
                const u32 index = lod.meshletOffset + meshletIndex;
                if (index < view.meshlets.size())
                    record.lod0Triangles += view.meshlets[index].triangleCount;
            }
        }
        record.positionBase = AppendSpan(_positions, view.positions);
        record.numPositions = static_cast<u32>(view.positions.size());
        record.vertexAttributeBase = AppendSpan(_vertexAttributes, view.vertexAttributes);
        record.numVertexAttributes = static_cast<u32>(view.vertexAttributes.size());
        record.skinningDataBase = AppendSpan(_skinningData, view.skinningData);
        record.numSkinningData = static_cast<u32>(view.skinningData.size());
        record.meshletVertexIndexBase = AppendSpan(_meshletVertexIndices, view.meshletVertexIndices);
        record.numMeshletVertexIndices = static_cast<u32>(view.meshletVertexIndices.size());
        record.meshletTriangleBase = AppendSpan(_meshletTriangles, view.meshletTriangles);
        record.numMeshletTriangles = static_cast<u32>(view.meshletTriangles.size());
        record.jointPaletteRemapBase = AppendSpan(_jointPaletteRemaps, view.jointPaletteRemaps);
        record.numJointPaletteRemaps = static_cast<u32>(view.jointPaletteRemaps.size());
        record.materialSlotBase = AppendSpan(_materialSlots, view.materialSlots);
        record.numMaterialSlots = static_cast<u32>(view.materialSlots.size());
        record.embeddedInstanceSetBase = AppendSpan(_embeddedInstanceSets, view.embeddedInstanceSets);
        record.numEmbeddedInstanceSets = static_cast<u32>(view.embeddedInstanceSets.size());
        record.embeddedInstanceBase = AppendSpan(_embeddedInstances, view.embeddedInstances);
        record.numEmbeddedInstances = static_cast<u32>(view.embeddedInstances.size());
        record.defaultMaterialTableOffset = materialTableOffset;
        record.defaultMaterialTableCount = materialTableCount;
        record.flags = view.root.flags;
        record.geometryGroupCount = view.root.geometryGroupCount;

        outHandle = RenderAssets::ModelHandle(_records.Add(record));
        const ParameterRange range{
            .parameterOffset = static_cast<u32>(_parameters.size()),
            .parameterCount = static_cast<u32>(view.parameters.size()),
            .bindingOffset = static_cast<u32>(_parameterBindings.size()),
            .bindingCount = static_cast<u32>(view.parameterBindings.size())};
        _parameters.insert(_parameters.end(), view.parameters.begin(), view.parameters.end());
        _parameterBindings.insert(_parameterBindings.end(), view.parameterBindings.begin(),
                                  view.parameterBindings.end());
        _parameterRanges.push_back(range);
        return outHandle != RenderAssets::ModelHandle::Invalid();
    }

    void ModelGeometryStorage::SyncToGPU(Renderer::Renderer* renderer)
    {
#define SYNC_BUFFER(member) _bufferGrowths += member.SyncToGPU(renderer) ? 1u : 0u
        SYNC_BUFFER(_records);
        SYNC_BUFFER(_meshes);
        SYNC_BUFFER(_meshLODs);
        SYNC_BUFFER(_submeshes);
        SYNC_BUFFER(_meshlets);
        SYNC_BUFFER(_positions);
        SYNC_BUFFER(_vertexAttributes);
        SYNC_BUFFER(_skinningData);
        SYNC_BUFFER(_meshletVertexIndices);
        SYNC_BUFFER(_meshletTriangles);
        SYNC_BUFFER(_jointPaletteRemaps);
        SYNC_BUFFER(_materialSlots);
        SYNC_BUFFER(_embeddedInstanceSets);
        SYNC_BUFFER(_embeddedInstances);
#undef SYNC_BUFFER
    }

    bool ModelGeometryStorage::FindMaterialSlot(RenderAssets::ModelHandle handle, u32 stableID, u32& outSlot) const
    {
        if (!HasModel(handle))
            return false;

        const ModelGPURecord& model = GetRecord(handle);
        for (u32 slot = 0; slot < model.numMaterialSlots; ++slot)
        {
            if (_materialSlots[model.materialSlotBase + slot].stableID != stableID)
                continue;

            outSlot = slot;
            return true;
        }
        return false;
    }

    std::span<const FileFormat::Model::Parameter> ModelGeometryStorage::GetParameters(
        RenderAssets::ModelHandle handle) const
    {
        const u32 index = static_cast<RenderAssets::ModelHandle::type>(handle);
        if (!HasModel(handle) || index >= _parameterRanges.size())
            return {};
        const ParameterRange& range = _parameterRanges[index];
        return {_parameters.data() + range.parameterOffset, range.parameterCount};
    }

    std::span<const FileFormat::Model::ParameterBinding> ModelGeometryStorage::GetParameterBindings(
        RenderAssets::ModelHandle handle) const
    {
        const u32 index = static_cast<RenderAssets::ModelHandle::type>(handle);
        if (!HasModel(handle) || index >= _parameterRanges.size())
            return {};
        const ParameterRange& range = _parameterRanges[index];
        return {_parameterBindings.data() + range.bindingOffset, range.bindingCount};
    }

    ModelGeometryStorageStats ModelGeometryStorage::GetStats() const
    {
        ModelGeometryStorageStats stats;
        stats.numModels = _records.Count();
        stats.numMeshes = _meshes.Count();
        stats.numMeshLODs = _meshLODs.Count();
        stats.numSubmeshes = _submeshes.Count();
        stats.numMeshlets = _meshlets.Count();
        stats.numPositions = _positions.Count();
#define ADD_BYTES(member)                                                                                                                                        \
    stats.usedBytes += member.UsedBytes();                                                                                                                       \
    stats.reservedBytes += member.TotalBytes()
        ADD_BYTES(_records);
        ADD_BYTES(_meshes);
        ADD_BYTES(_meshLODs);
        ADD_BYTES(_submeshes);
        ADD_BYTES(_meshlets);
        ADD_BYTES(_positions);
        ADD_BYTES(_vertexAttributes);
        ADD_BYTES(_skinningData);
        ADD_BYTES(_meshletVertexIndices);
        ADD_BYTES(_meshletTriangles);
        ADD_BYTES(_jointPaletteRemaps);
        ADD_BYTES(_materialSlots);
        ADD_BYTES(_embeddedInstanceSets);
        ADD_BYTES(_embeddedInstances);
#undef ADD_BYTES
        stats.bufferGrowths = _bufferGrowths;
        return stats;
    }
} // namespace ModelLoading
