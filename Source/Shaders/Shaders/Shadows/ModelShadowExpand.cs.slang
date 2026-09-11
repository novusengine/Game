#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Material.inc.slang"
#include "Shadows/ModelShadow.inc.slang"
#include "Shadows/SVSM.inc.slang"

struct Constants
{
    uint casterCount;
    uint numClipmaps;
    uint queueCapacity;
    int forcedLOD;
    float lodTargetTexels;
    uint dynamicSplit;
    uint frameIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _shadowModelInstances;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelRecord> _shadowModels;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelMesh> _shadowMeshes;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelLOD> _shadowLODs;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelSubmesh> _shadowSubmeshes;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<uint> _shadowGeometryGroupMasks;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<uint> _shadowMaterialTable;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<SVSMData> _shadowSVSMData;
[[vk::binding(8, PER_PASS)]] RWStructuredBuffer<ModelShadowChunk> _shadowChunks0;
[[vk::binding(9, PER_PASS)]] RWStructuredBuffer<uint> _shadowStats0;
[[vk::binding(10, PER_PASS)]] RWStructuredBuffer<ModelShadowChunk> _shadowChunks1;
[[vk::binding(11, PER_PASS)]] RWStructuredBuffer<uint> _shadowStats1;
[[vk::binding(12, PER_PASS)]] StructuredBuffer<uint> _shadowCasterInstances;

void AddStat(uint frameIndex, uint index, uint value)
{
    if (frameIndex == 0u) InterlockedAdd(_shadowStats0[index], value);
    else InterlockedAdd(_shadowStats1[index], value);
}

void AddStat(uint frameIndex, uint index, uint value, out uint previous)
{
    if (frameIndex == 0u) InterlockedAdd(_shadowStats0[index], value, previous);
    else InterlockedAdd(_shadowStats1[index], value, previous);
}

void StoreChunk(uint frameIndex, uint index, ModelShadowChunk chunk)
{
    if (frameIndex == 0u) _shadowChunks0[index] = chunk;
    else _shadowChunks1[index] = chunk;
}

bool GeometryGroupEnabled(ModelInstanceRecord instance, uint groupID)
{
    uint word = groupID >> 5u;
    uint boundWordCount;
    uint boundWordStride;
    _shadowGeometryGroupMasks.GetDimensions(boundWordCount, boundWordStride);
    uint absoluteWord = instance.geometryGroupWordOffset + word;
    return word < instance.geometryGroupWordCount && absoluteWord >= instance.geometryGroupWordOffset &&
           absoluteWord < boundWordCount &&
           (_shadowGeometryGroupMasks[absoluteWord] & (1u << (groupID & 31u))) != 0u;
}

uint SelectShadowLOD(ModelMesh mesh, ModelRecord model, ModelInstanceRecord instance, uint clipmapIndex)
{
    if (_constants.forcedLOD >= 0)
        return min(uint(_constants.forcedLOD), mesh.numLODs - 1u);

    float texelWorld = _shadowSVSMData[0].extent[clipmapIndex] /
        float(_shadowSVSMData[0].configPageTableSize * _shadowSVSMData[0].configPageSize);
    float allowedError = texelWorld * _constants.lodTargetTexels;
    float worldScale = MaxWorldScale(instance.currentWorld);
    uint selected = 0u;
    for (uint localLOD = mesh.numLODs; localLOD-- > 1u;)
    {
        if (_shadowLODs[model.meshLODBase + mesh.lodOffset + localLOD].geometricError * worldScale <= allowedError)
        {
            selected = localLOD;
            break;
        }
    }
    return selected;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint casterIndex = dispatchThreadID.x;
    uint clipmapIndex = dispatchThreadID.y;
    uint frameIndex = _constants.frameIndex;
    if (casterIndex >= _constants.casterCount || clipmapIndex >= _constants.numClipmaps)
        return;

    uint instanceIndex = _shadowCasterInstances[casterIndex];
    ModelInstanceRecord instance = _shadowModelInstances[instanceIndex];
    AddStat(frameIndex, MODEL_SHADOW_STAT_TESTED_INSTANCES, 1u);
    if ((instance.flags & MODEL_INSTANCE_VISIBLE) == 0u ||
        (instance.flags & MODEL_INSTANCE_CASTS_SHADOWS) == 0u || instance.opacity <= 0.0f)
    {
        AddStat(frameIndex, MODEL_SHADOW_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    bool dynamicCaster = _constants.dynamicSplit != 0u &&
        (instance.flags & MODEL_INSTANCE_DYNAMIC_SHADOW_CASTER) != 0u;
    if (dynamicCaster ? _shadowSVSMData[0].statsDynamicLive[clipmapIndex] == 0u :
                        _shadowSVSMData[0].statsDirty[clipmapIndex] == 0u)
        return;

    ModelRecord model = _shadowModels[instance.modelIndex];
    Camera camera = _cameras[clipmapIndex + 1u];
    float4 worldBounds = TransformSphere(model.bounds.center, model.bounds.sphereRadius, instance.currentWorld);
    if (instance.deformationHandle == 0xFFFFFFFFu && !ModelSphereInsideFrustum(camera, worldBounds))
    {
        AddStat(frameIndex, MODEL_SHADOW_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    for (uint meshIndex = 0u; meshIndex < model.numMeshes; ++meshIndex)
    {
        ModelMesh mesh = _shadowMeshes[model.meshBase + meshIndex];
        if (mesh.numLODs == 0u)
            continue;
        uint localLOD = SelectShadowLOD(mesh, model, instance, clipmapIndex);
        ModelLOD lod = _shadowLODs[model.meshLODBase + mesh.lodOffset + localLOD];
        if ((lod.flags & MODEL_LOD_HAS_SKINNING) != 0u &&
            (lod.flags & MODEL_LOD_STATIC_FALLBACK_IS_BIND_POSE) == 0u &&
            instance.deformationHandle == 0xFFFFFFFFu)
            continue;

        for (uint submeshIndex = 0u; submeshIndex < lod.numSubmeshes; ++submeshIndex)
        {
            ModelSubmesh submesh = _shadowSubmeshes[model.submeshBase + lod.submeshOffset + submeshIndex];
            if (meshIndex >= (1u << MODEL_VISIBILITY_MESH_BITS) ||
                localLOD >= (1u << MODEL_VISIBILITY_LOD_BITS) ||
                submeshIndex >= (1u << MODEL_VISIBILITY_SUBMESH_BITS) ||
                submesh.meshletOffset + submesh.numMeshlets > (1u << MODEL_VISIBILITY_MESHLET_BITS))
            {
                AddStat(frameIndex, MODEL_SHADOW_STAT_REJECTED_BOUNDS, submesh.numMeshlets);
                continue;
            }
            if (!GeometryGroupEnabled(instance, submesh.geometryGroupID))
            {
                AddStat(frameIndex, MODEL_SHADOW_STAT_REJECTED_GEOMETRY_GROUP, submesh.numMeshlets);
                continue;
            }

            uint materialIndex = instance.materialTableOffset + mesh.materialSlotOffset + submesh.materialSlotIndex;
            uint materialCount;
            uint materialStride;
            _shadowMaterialTable.GetDimensions(materialCount, materialStride);
            if (materialIndex < instance.materialTableOffset || materialIndex >= materialCount)
            {
                AddStat(frameIndex, MODEL_SHADOW_STAT_REJECTED_BOUNDS, submesh.numMeshlets);
                continue;
            }
            uint materialInstanceIndex = _shadowMaterialTable[materialIndex];
            MaterialInstanceRecord materialInstance = _materialInstances[materialInstanceIndex];
            if ((materialInstance.flags & MATERIAL_CASTS_SHADOWS) == 0u)
            {
                AddStat(frameIndex, MODEL_SHADOW_STAT_REJECTED_MATERIAL, submesh.numMeshlets);
                continue;
            }
            uint materialRasterClass = MaterialRasterClass(materialInstance);
            if (materialRasterClass > 1u)
            {
                AddStat(frameIndex, MODEL_SHADOW_STAT_REJECTED_TRANSPARENT, submesh.numMeshlets);
                continue;
            }
            uint rasterClass = materialRasterClass * 2u + ((materialInstance.flags & MATERIAL_TWO_SIDED) != 0u);
            uint numChunks = (submesh.numMeshlets + MODEL_SHADOW_CHUNK_MESHLET_COUNT - 1u) /
                             MODEL_SHADOW_CHUNK_MESHLET_COUNT;
            uint chunkBase;
            AddStat(frameIndex, MODEL_SHADOW_STAT_EXPANDED_CHUNKS, numChunks, chunkBase);
            uint committed = min(numChunks, _constants.queueCapacity - min(chunkBase, _constants.queueCapacity));
            if (committed < numChunks)
                AddStat(frameIndex, MODEL_SHADOW_STAT_CHUNK_OVERFLOWS, numChunks - committed);

            for (uint localChunk = 0u; localChunk < committed; ++localChunk)
            {
                uint chunkOffset = localChunk * MODEL_SHADOW_CHUNK_MESHLET_COUNT;
                ModelShadowChunk chunk;
                chunk.instanceIndex = instanceIndex;
                chunk.meshIndex = meshIndex;
                chunk.meshletBase = model.meshletBase + submesh.meshletOffset + chunkOffset;
                uint count = min(submesh.numMeshlets - chunkOffset, MODEL_SHADOW_CHUNK_MESHLET_COUNT);
                chunk.packedCountRasterLODAndClipmap = count | (rasterClass << MODEL_SHADOW_CHUNK_RASTER_SHIFT) |
                    (localLOD << MODEL_SHADOW_CHUNK_LOD_SHIFT) |
                    (clipmapIndex << MODEL_SHADOW_CHUNK_CLIPMAP_SHIFT);
                chunk.submeshIndex = submeshIndex;
                chunk.dynamicCaster = dynamicCaster ? 1u : 0u;
                chunk.reserved0 = 0u;
                chunk.reserved1 = 0u;
                StoreChunk(frameIndex, chunkBase + localChunk, chunk);
            }
        }
    }
}
