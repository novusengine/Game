#include "DescriptorSet/Global.inc.slang"
#define MODEL_DIAGNOSTIC_NO_RESOURCES
#include "Model/ModelDiagnostic.inc.slang"
#include "Model/ModelViewWork.inc.slang"

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

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ViewInstanceInput> _viewInputs;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _lodHistory;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelInstances;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelRecord> _modelRecords;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelMesh> _modelMeshes;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<ModelLOD> _modelLODs;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelSubmesh> _modelSubmeshes;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<uint> _geometryGroupMasks;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<uint> _materialTable;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<MaterialInstanceRecord> _materialInstances;
[[vk::binding(10, PER_PASS)]] StructuredBuffer<MaterialRecord> _materials;
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

void StoreChunk(uint index, ModelMeshletChunk chunk)
{
    if (_constants.resourceIndex == 0u) _chunkQueue0[index] = chunk;
    else _chunkQueue1[index] = chunk;
}

uint SelectLOD(ModelMesh mesh, ModelRecord model, ModelInstanceRecord instance, Camera camera, uint meshIndex,
               uint lodHistoryOffset)
{
    if (_constants.forcedLOD >= 0)
        return min(uint(_constants.forcedLOD), mesh.numLODs - 1u);

    float4 sphere = TransformSphere(mesh.bounds.center, mesh.bounds.sphereRadius, instance.currentWorld);
    float distanceToEye = max(length(sphere.xyz - camera.eyePosition.xyz) - sphere.w, 0.001f);
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

    uint historyIndex = lodHistoryOffset + meshIndex;
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
    return word < instance.geometryGroupWordCount &&
           (_geometryGroupMasks[instance.geometryGroupWordOffset + word] & (1u << (groupID & 31u))) != 0u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= _constants.inputCount)
        return;

    ViewInstanceInput input = _viewInputs[DispatchThreadID.x];
    ModelInstanceRecord instance = _modelInstances[input.instanceIndex];
    AddStat(1u, 1u);
    if ((instance.flags & MODEL_INSTANCE_VISIBLE) == 0u)
    {
        AddStat(2u, 1u);
        return;
    }

    ModelRecord model = _modelRecords[instance.modelIndex];
    Camera camera = _cameras[_constants.viewIndex];
    if (!ModelSphereInsideFrustum(camera, TransformSphere(model.bounds.center, model.bounds.sphereRadius,
                                                           instance.currentWorld)))
    {
        AddStat(2u, 1u);
        return;
    }

    for (uint meshIndex = 0u; meshIndex < model.numMeshes; ++meshIndex)
    {
        ModelMesh mesh = _modelMeshes[model.meshBase + meshIndex];
        if (mesh.numLODs == 0u)
            continue;

        uint localLOD = SelectLOD(mesh, model, instance, camera, meshIndex, input.lodHistoryOffset);
        ModelLOD lod = _modelLODs[model.meshLODBase + mesh.lodOffset + localLOD];
        if ((lod.flags & MODEL_LOD_HAS_SKINNING) != 0u &&
            (lod.flags & MODEL_LOD_STATIC_FALLBACK_IS_BIND_POSE) == 0u)
        {
            AddStat(12u, 1u);
            continue;
        }
        AddStat(13u + min(localLOD, 7u), 1u);

        for (uint submeshIndex = 0u; submeshIndex < lod.numSubmeshes; ++submeshIndex)
        {
            ModelSubmesh submesh = _modelSubmeshes[model.submeshBase + lod.submeshOffset + submeshIndex];
            if (!GeometryGroupEnabled(instance, submesh.geometryGroupID))
            {
                AddStat(8u, submesh.numMeshlets);
                continue;
            }

            uint materialInstanceIndex =
                _materialTable[instance.materialTableOffset + mesh.materialSlotOffset + submesh.materialSlotIndex];
            uint rasterClass =
                (_materials[_materialInstances[materialInstanceIndex].materialIndex].flags & MATERIAL_TWO_SIDED) != 0u;

            uint numChunks = (submesh.numMeshlets + 31u) / 32u;
            uint chunkBase;
            AddStat(3u, numChunks, chunkBase);
            uint committedChunks = min(numChunks, _constants.queueCapacity - min(chunkBase, _constants.queueCapacity));
            if (committedChunks < numChunks)
                AddStat(11u, numChunks - committedChunks);

            for (uint localChunk = 0u; localChunk < committedChunks; ++localChunk)
            {
                uint chunkOffset = localChunk * 32u;
                ModelMeshletChunk chunk;
                chunk.instanceIndex = input.instanceIndex;
                chunk.meshIndex = meshIndex;
                chunk.meshletBase = model.meshletBase + submesh.meshletOffset + chunkOffset;
                uint meshletCount = min(submesh.numMeshlets - chunkOffset, 32u);
                chunk.packedCountRasterAndLOD = meshletCount | (rasterClass << 6u) | (localLOD << 8u);
                chunk.positionBase = model.positionBase + lod.vertexOffset;
                chunk.vertexAttributeBase = model.vertexAttributeBase + lod.vertexAttributeOffset;
                chunk.meshletVertexIndexBase = model.meshletVertexIndexBase;
                chunk.meshletTriangleBase = model.meshletTriangleBase;
                StoreChunk(chunkBase + localChunk, chunk);
            }
        }
    }
}
