#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Material.inc.slang"
#include "Model/ModelGeometry.inc.slang"
#include "Model/ModelTransparentWork.inc.slang"
#include "Model/ModelViewWork.inc.slang"
#include "Model/ModelVisibilityRecord.inc.slang"

struct Constants
{
    uint viewIndex;
    uint inputCount;
    uint chunkCapacity;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ViewInstanceInput> _transparentViewInputs;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _transparentLODHistory;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _transparentInstances;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelRecord> _transparentModels;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelMesh> _transparentMeshes;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<ModelLOD> _transparentLODs;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelSubmesh> _transparentSubmeshes;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<uint> _transparentGeometryGroups;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<uint> _transparentMaterialTable;
[[vk::binding(9, PER_PASS)]] RWStructuredBuffer<ModelTransparentChunk> _transparentChunks0;
[[vk::binding(10, PER_PASS)]] RWStructuredBuffer<ModelTransparentChunk> _transparentChunks1;
[[vk::binding(11, PER_PASS)]] RWStructuredBuffer<uint> _transparentStats0;
[[vk::binding(12, PER_PASS)]] RWStructuredBuffer<uint> _transparentStats1;

void AddStat(uint index, uint value)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_transparentStats0[index], value);
    else InterlockedAdd(_transparentStats1[index], value);
}

void AddStat(uint index, uint value, out uint previous)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_transparentStats0[index], value, previous);
    else InterlockedAdd(_transparentStats1[index], value, previous);
}

void StoreChunk(uint index, ModelTransparentChunk chunk)
{
    if (_constants.resourceIndex == 0u) _transparentChunks0[index] = chunk;
    else _transparentChunks1[index] = chunk;
}

bool GeometryGroupEnabled(ModelInstanceRecord instance, uint groupID)
{
    uint word = groupID >> 5u;
    return word < instance.geometryGroupWordCount &&
           (_transparentGeometryGroups[instance.geometryGroupWordOffset + word] & (1u << (groupID & 31u))) != 0u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= _constants.inputCount)
        return;

    ViewInstanceInput input = _transparentViewInputs[dispatchThreadID.x];
    ModelInstanceRecord instance = _transparentInstances[input.instanceIndex];
    AddStat(MODEL_TRANSPARENT_STAT_TESTED_INSTANCES, 1u);
    if ((instance.flags & MODEL_INSTANCE_VISIBLE) == 0u || instance.opacity <= 0.0f)
    {
        AddStat(MODEL_TRANSPARENT_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    ModelRecord model = _transparentModels[instance.modelIndex];
    Camera camera = _cameras[_constants.viewIndex];
    if (!ModelSphereInsideFrustum(camera, TransformSphere(model.bounds.center, model.bounds.sphereRadius, instance.currentWorld)))
    {
        AddStat(MODEL_TRANSPARENT_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    bool forceTransparent = (instance.flags & MODEL_INSTANCE_FORCE_TRANSPARENT) != 0u;
    for (uint meshIndex = 0u; meshIndex < model.numMeshes; ++meshIndex)
    {
        ModelMesh mesh = _transparentMeshes[model.meshBase + meshIndex];
        if (mesh.numLODs == 0u)
            continue;
        uint localLOD = min(_transparentLODHistory[input.lodHistoryOffset + meshIndex], mesh.numLODs - 1u);
        ModelLOD lod = _transparentLODs[model.meshLODBase + mesh.lodOffset + localLOD];
        if ((lod.flags & MODEL_LOD_HAS_SKINNING) != 0u && (lod.flags & MODEL_LOD_STATIC_FALLBACK_IS_BIND_POSE) == 0u)
            continue;

        for (uint submeshIndex = 0u; submeshIndex < lod.numSubmeshes; ++submeshIndex)
        {
            ModelSubmesh submesh = _transparentSubmeshes[model.submeshBase + lod.submeshOffset + submeshIndex];
            if (!GeometryGroupEnabled(instance, submesh.geometryGroupID))
            {
                AddStat(MODEL_TRANSPARENT_STAT_REJECTED_GEOMETRY_GROUP_MESHLETS, submesh.numMeshlets);
                continue;
            }
            if (meshIndex >= (1u << MODEL_VISIBILITY_MESH_BITS) || localLOD >= (1u << MODEL_VISIBILITY_LOD_BITS) ||
                submeshIndex >= (1u << MODEL_VISIBILITY_SUBMESH_BITS) ||
                submesh.meshletOffset + submesh.numMeshlets > (1u << MODEL_VISIBILITY_MESHLET_BITS))
            {
                AddStat(MODEL_TRANSPARENT_STAT_VISIBILITY_RECORD_PACKING_FAILURES, submesh.numMeshlets);
                continue;
            }

            uint materialInstanceIndex = _transparentMaterialTable[instance.materialTableOffset + mesh.materialSlotOffset + submesh.materialSlotIndex];
            MaterialInstanceRecord materialInstance = _materialInstances[materialInstanceIndex];
            uint executionGroup = MaterialExecutionGroup(materialInstance);
            if (!forceTransparent && MaterialRasterClass(materialInstance) != 2u)
                continue;
            if (forceTransparent)
                executionGroup = MaterialBaseExecutionGroup(_materials[materialInstance.materialIndex]) + 4u;
            uint binIndex = ModelTransparentBin(executionGroup, (materialInstance.flags & MATERIAL_TWO_SIDED) != 0u);
            if (binIndex >= MODEL_TRANSPARENT_BIN_COUNT)
                continue;

            uint numChunks = (submesh.numMeshlets + MODEL_CHUNK_MESHLET_COUNT - 1u) / MODEL_CHUNK_MESHLET_COUNT;
            uint chunkBase;
            AddStat(MODEL_TRANSPARENT_STAT_EXPANDED_CHUNKS, numChunks, chunkBase);
            uint committedChunks = min(numChunks, _constants.chunkCapacity - min(chunkBase, _constants.chunkCapacity));
            if (committedChunks < numChunks)
                AddStat(MODEL_TRANSPARENT_STAT_CHUNK_QUEUE_OVERFLOWS, numChunks - committedChunks);
            for (uint localChunk = 0u; localChunk < committedChunks; ++localChunk)
            {
                uint chunkOffset = localChunk * MODEL_CHUNK_MESHLET_COUNT;
                ModelTransparentChunk chunk;
                chunk.instanceIndex = input.instanceIndex;
                chunk.meshIndex = meshIndex;
                chunk.meshletBase = model.meshletBase + submesh.meshletOffset + chunkOffset;
                chunk.packedCountAndLOD = min(submesh.numMeshlets - chunkOffset, MODEL_CHUNK_MESHLET_COUNT) |
                    (localLOD << MODEL_TRANSPARENT_LOD_SHIFT);
                chunk.submeshIndex = submeshIndex;
                chunk.binIndex = binIndex;
                chunk.reserved0 = 0u;
                chunk.reserved1 = 0u;
                StoreChunk(chunkBase + localChunk, chunk);
            }
        }
    }
}
