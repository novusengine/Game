#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Material.inc.slang"
#include "Model/ModelViewWork.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint viewIndex;
    uint inputCount;
    uint queueCapacity;
    int forcedLOD;
    float viewportHeight;
    float lodTargetPixels;
    float lodHysteresis;
    uint resetHistory;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ViewInstanceInput> _selectedInputs0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ViewInstanceInput> _selectedInputs1;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _lodHistory;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelInstances;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelRecord> _modelRecords;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<ModelMesh> _modelMeshes;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelLOD> _modelLODs;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<ModelSubmesh> _modelSubmeshes;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<uint> _geometryGroupMasks;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<uint> _materialTable;
[[vk::binding(11, PER_PASS)]] RWStructuredBuffer<ModelMeshletChunk> _chunkQueue0;
[[vk::binding(12, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(13, PER_PASS)]] RWStructuredBuffer<ModelMeshletChunk> _chunkQueue1;
[[vk::binding(14, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;

void AddStat(uint index, uint value)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_workStats0[index], value);
    else InterlockedAdd(_workStats1[index], value);
}

void AddStat(uint index, uint value, out uint previous)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_workStats0[index], value, previous);
    else InterlockedAdd(_workStats1[index], value, previous);
}

uint LoadStat(uint index)
{
    return _constants.resourceIndex == 0u ? _workStats0[index] : _workStats1[index];
}

void StoreChunk(uint index, ModelMeshletChunk chunk)
{
    if (_constants.resourceIndex == 0u) _chunkQueue0[index] = chunk;
    else _chunkQueue1[index] = chunk;
}

uint SelectLOD(ModelMesh mesh, ModelRecord model, ModelInstanceRecord instance, Camera camera, uint meshIndex,
               uint lodHistoryOffset)
{
    uint historyIndex = lodHistoryOffset + meshIndex;
    if (_constants.forcedLOD >= 0)
    {
        uint selected = min(uint(_constants.forcedLOD), mesh.numLODs - 1u);
        _lodHistory[historyIndex] = selected;
        return selected;
    }

    float4 sphere = TransformSphere(mesh.bounds.center, mesh.bounds.sphereRadius, instance.currentWorld);
    float distanceToEye = max(length(sphere.xyz - camera.cullingEyePosition.xyz) - sphere.w, 0.001f);
    float projectionScale = 0.5f * _constants.viewportHeight * abs(camera.viewToClip[1][1]) /
                            distanceToEye * MaxWorldScale(instance.currentWorld);
    uint selected = 0u;
    for (uint localLOD = mesh.numLODs; localLOD-- > 1u;)
    {
        float pixels = _modelLODs[model.meshLODBase + mesh.lodOffset + localLOD].geometricError * projectionScale;
        if (pixels <= _constants.lodTargetPixels)
        {
            selected = localLOD;
            break;
        }
    }

    uint previous = _constants.resetHistory != 0u ? INVALID_LOD_HISTORY : _lodHistory[historyIndex];
    if (previous < mesh.numLODs && previous != selected)
    {
        float previousPixels = _modelLODs[model.meshLODBase + mesh.lodOffset + previous].geometricError * projectionScale;
        if (selected > previous)
        {
            float selectedPixels = _modelLODs[model.meshLODBase + mesh.lodOffset + selected].geometricError * projectionScale;
            if (selectedPixels > _constants.lodTargetPixels * (1.0f - _constants.lodHysteresis))
                selected = previous;
        }
        else if (previousPixels < _constants.lodTargetPixels * (1.0f + _constants.lodHysteresis))
        {
            selected = previous;
        }
    }
    _lodHistory[historyIndex] = selected;
    return selected;
}

bool GeometryGroupEnabled(ModelInstanceRecord instance, uint groupID)
{
    uint word = groupID >> 5u;
    uint boundWordCount;
    uint boundWordStride;
    _geometryGroupMasks.GetDimensions(boundWordCount, boundWordStride);
    uint absoluteWord = instance.geometryGroupWordOffset + word;
    return word < instance.geometryGroupWordCount && absoluteWord >= instance.geometryGroupWordOffset &&
           absoluteWord < boundWordCount &&
           (_geometryGroupMasks[absoluteWord] & (1u << (groupID & 31u))) != 0u;
}

groupshared uint _selectedLOD;
groupshared uint _selectedLODValid;

[shader("compute")]
[numthreads(16, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint selectedCapacity;
    uint selectedStride;
    if (_constants.resourceIndex == 0u) _selectedInputs0.GetDimensions(selectedCapacity, selectedStride);
    else _selectedInputs1.GetDimensions(selectedCapacity, selectedStride);
    uint selectedCount = min(LoadStat(MODEL_WORK_STAT_SELECTED_INSTANCES), selectedCapacity);
    uint groupsX = selectedCount <= MODEL_COMPUTE_DISPATCH_LINEAR_THRESHOLD ? max(selectedCount, 1u) : MODEL_COMPUTE_DISPATCH_MAX_GROUPS_X;
    uint inputIndex = groupID.x + groupID.y * groupsX;
    if (inputIndex >= selectedCount)
        return;
    uint lane = groupThreadID.x;
    ViewInstanceInput input = _constants.resourceIndex == 0u ? _selectedInputs0[inputIndex] :
                                                                _selectedInputs1[inputIndex];
    ModelInstanceRecord instance = _modelInstances[input.instanceIndex];
    ModelRecord model = _modelRecords[instance.modelIndex];
    Camera camera = _cameras[_constants.viewIndex];

    uint boundMeshCount;
    uint boundMeshStride;
    _modelMeshes.GetDimensions(boundMeshCount, boundMeshStride);
    uint boundLODCount;
    uint boundLODStride;
    _modelLODs.GetDimensions(boundLODCount, boundLODStride);
    uint boundSubmeshCount;
    uint boundSubmeshStride;
    _modelSubmeshes.GetDimensions(boundSubmeshCount, boundSubmeshStride);
    uint boundLODHistoryCount;
    uint boundLODHistoryStride;
    _lodHistory.GetDimensions(boundLODHistoryCount, boundLODHistoryStride);
    uint boundMaterialTableCount;
    uint boundMaterialTableStride;
    _materialTable.GetDimensions(boundMaterialTableCount, boundMaterialTableStride);
    uint safeMeshCount = min(model.numMeshes, boundMeshCount - min(model.meshBase, boundMeshCount));
    for (uint meshIndex = 0u; meshIndex < safeMeshCount; ++meshIndex)
    {
        uint absoluteMeshIndex = model.meshBase + meshIndex;
        if (absoluteMeshIndex < model.meshBase || absoluteMeshIndex >= boundMeshCount)
            continue;
        ModelMesh mesh = _modelMeshes[absoluteMeshIndex];
        uint lodBase = model.meshLODBase + mesh.lodOffset;
        uint lodEnd = lodBase + mesh.numLODs;
        uint historyIndex = input.lodHistoryOffset + meshIndex;
        if (lane == 0u)
        {
            _selectedLODValid = mesh.numLODs != 0u && lodBase >= model.meshLODBase && lodEnd >= lodBase &&
                lodEnd <= boundLODCount && historyIndex >= input.lodHistoryOffset &&
                historyIndex < boundLODHistoryCount ? 1u : 0u;
            if (_selectedLODValid != 0u)
                _selectedLOD = SelectLOD(mesh, model, instance, camera, meshIndex, input.lodHistoryOffset);
        }
        GroupMemoryBarrierWithGroupSync();
        if (_selectedLODValid == 0u)
            continue;

        uint localLOD = _selectedLOD;
        ModelLOD lod = _modelLODs[lodBase + localLOD];
        if ((lod.flags & MODEL_LOD_HAS_SKINNING) != 0u &&
            (lod.flags & MODEL_LOD_STATIC_FALLBACK_IS_BIND_POSE) == 0u &&
            instance.deformationHandle == 0xFFFFFFFFu)
        {
            if (lane == 0u)
                AddStat(MODEL_WORK_STAT_SKIPPED_SKINNED_LODS, 1u);
            continue;
        }
        if (lane == 0u)
            AddStat(MODEL_WORK_STAT_LOD_SELECTION_BASE + min(localLOD, MODEL_WORK_STAT_LOD_SELECTION_COUNT - 1u), 1u);

        uint submeshBase = model.submeshBase + lod.submeshOffset;
        uint safeSubmeshCount = submeshBase < model.submeshBase ? 0u :
            min(lod.numSubmeshes, boundSubmeshCount - min(submeshBase, boundSubmeshCount));
        for (uint submeshIndex = lane; submeshIndex < safeSubmeshCount; submeshIndex += 16u)
        {
            uint absoluteSubmeshIndex = submeshBase + submeshIndex;
            if (submeshBase < model.submeshBase || absoluteSubmeshIndex < submeshBase ||
                absoluteSubmeshIndex >= boundSubmeshCount)
                continue;
            ModelSubmesh submesh = _modelSubmeshes[absoluteSubmeshIndex];
            if (meshIndex >= (1u << MODEL_VISIBILITY_MESH_BITS) ||
                localLOD >= (1u << MODEL_VISIBILITY_LOD_BITS) ||
                submeshIndex >= (1u << MODEL_VISIBILITY_SUBMESH_BITS) ||
                submesh.meshletOffset + submesh.numMeshlets > (1u << MODEL_VISIBILITY_MESHLET_BITS))
            {
                AddStat(MODEL_WORK_STAT_VISIBILITY_RECORD_PACKING_FAILURES, submesh.numMeshlets);
                continue;
            }
            if (!GeometryGroupEnabled(instance, submesh.geometryGroupID))
            {
                AddStat(MODEL_WORK_STAT_REJECTED_GEOMETRY_GROUP_MESHLETS, submesh.numMeshlets);
                continue;
            }

            uint materialIndex = instance.materialTableOffset + mesh.materialSlotOffset + submesh.materialSlotIndex;
            if (materialIndex < instance.materialTableOffset || materialIndex >= boundMaterialTableCount)
                continue;
            uint materialInstanceIndex = _materialTable[materialIndex];
            MaterialInstanceRecord materialInstance = _materialInstances[materialInstanceIndex];
            uint materialRasterClass = MaterialRasterClass(materialInstance);
            if (materialRasterClass > 1u)
                continue;
            uint rasterClass = materialRasterClass * 2u + ((materialInstance.flags & MATERIAL_TWO_SIDED) != 0u);
            uint resolveClass = MaterialResolveGroup(MaterialExecutionGroup(materialInstance)) & 1u;

            uint numChunks = (submesh.numMeshlets + MODEL_CHUNK_MESHLET_COUNT - 1u) / MODEL_CHUNK_MESHLET_COUNT;
            uint chunkBase;
            AddStat(MODEL_WORK_STAT_EXPANDED_CHUNKS, numChunks, chunkBase);
            uint committedChunks = min(numChunks, _constants.queueCapacity - min(chunkBase, _constants.queueCapacity));
            if (committedChunks < numChunks)
                AddStat(MODEL_WORK_STAT_CHUNK_QUEUE_OVERFLOWS, numChunks - committedChunks);

            for (uint localChunk = 0u; localChunk < committedChunks; ++localChunk)
            {
                uint chunkOffset = localChunk * MODEL_CHUNK_MESHLET_COUNT;
                ModelMeshletChunk chunk;
                chunk.packedInstanceAndMesh = PackModelInstanceAndMesh(input.instanceIndex, meshIndex);
                chunk.meshletBase = model.meshletBase + submesh.meshletOffset + chunkOffset;
                uint meshletCount = min(submesh.numMeshlets - chunkOffset, MODEL_CHUNK_MESHLET_COUNT);
                chunk.packedCountRasterAndLOD = meshletCount |
                    (rasterClass << MODEL_CHUNK_RASTER_CLASS_SHIFT) |
                    (localLOD << MODEL_CHUNK_LOD_SHIFT) |
                    (submeshIndex << MODEL_CHUNK_SUBMESH_SHIFT) |
                    (resolveClass << MODEL_CHUNK_RESOLVE_CLASS_SHIFT);
                chunk.lodHistoryIndex = input.lodHistoryOffset + meshIndex;
                StoreChunk(chunkBase + localChunk, chunk);
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
