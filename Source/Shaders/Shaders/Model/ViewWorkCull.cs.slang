#include "DescriptorSet/Global.inc.slang"
#define MODEL_DIAGNOSTIC_NO_RESOURCES
#include "Model/ModelDiagnostic.inc.slang"
#include "Model/ModelViewWork.inc.slang"

struct Constants
{
    uint viewIndex;
    uint queueCapacity;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelMeshletChunk> _chunkQueue0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelInstances;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelRecord> _modelRecords;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelMesh> _modelMeshes;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelMeshlet> _modelMeshlets;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<DiagnosticMeshletWork> _oneSidedQueue0;
[[vk::binding(6, PER_PASS)]] RWStructuredBuffer<DiagnosticMeshletWork> _twoSidedQueue0;
[[vk::binding(7, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<ModelMeshletChunk> _chunkQueue1;
[[vk::binding(9, PER_PASS)]] RWStructuredBuffer<DiagnosticMeshletWork> _oneSidedQueue1;
[[vk::binding(10, PER_PASS)]] RWStructuredBuffer<DiagnosticMeshletWork> _twoSidedQueue1;
[[vk::binding(11, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;

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

ModelMeshletChunk LoadChunk(uint index)
{
    return _constants.resourceIndex == 0u ? _chunkQueue0[index] : _chunkQueue1[index];
}

void StoreWork(uint rasterClass, uint index, DiagnosticMeshletWork work)
{
    if (_constants.resourceIndex == 0u)
    {
        if (rasterClass == 0u) _oneSidedQueue0[index] = work;
        else _twoSidedQueue0[index] = work;
    }
    else
    {
        if (rasterClass == 0u) _oneSidedQueue1[index] = work;
        else _twoSidedQueue1[index] = work;
    }
}

uint MixSeed(uint seed, uint value)
{
    seed ^= value + 0x9E3779B9u + (seed << 6u) + (seed >> 2u);
    seed ^= seed >> 16u;
    seed *= 0x7FEB352Du;
    return seed ^ (seed >> 15u);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 GroupID : SV_GroupID, uint3 GroupThreadID : SV_GroupThreadID)
{
    ModelMeshletChunk chunk = LoadChunk(GroupID.x);
    uint meshletCount = chunk.packedCountRasterAndLOD & 0x3Fu;
    if (GroupThreadID.x == 0u)
    {
        AddStat(4u, 1u);
        AddStat(5u, meshletCount);
    }

    uint meshletIndex = chunk.meshletBase + GroupThreadID.x;
    ModelInstanceRecord instance = _modelInstances[chunk.instanceIndex];
    Camera camera = _cameras[_constants.viewIndex];
    uint rasterClass = (chunk.packedCountRasterAndLOD >> 6u) & 0x1u;
    bool visible = GroupThreadID.x < meshletCount;
    ModelMeshlet meshlet;
    if (visible)
    {
        meshlet = _modelMeshlets[meshletIndex];
        float4 sphere = TransformSphere(meshlet.boundsCenter, meshlet.boundsRadius, instance.currentWorld);
        if (!ModelSphereInsideFrustum(camera, sphere))
        {
            AddStat(6u, 1u);
            visible = false;
        }
        else if (rasterClass == 0u && ModelConeBackfacing(meshlet.packedNormalCone, meshlet.boundsCenter,
                                                          meshlet.boundsRadius, instance.currentWorld,
                                                          camera.eyePosition.xyz))
        {
            AddStat(7u, 1u);
            visible = false;
        }
    }

    uint visibleCount = WaveActiveCountBits(visible);
    uint waveOffset = 0u;
    uint countIndex = rasterClass == 0u ? 9u : 10u;
    if (WaveIsFirstLane())
    {
        AddStat(countIndex, visibleCount, waveOffset);
        uint committedCount = min(visibleCount, _constants.queueCapacity - min(waveOffset, _constants.queueCapacity));
        if (committedCount < visibleCount)
            AddStat(11u, visibleCount - committedCount);
    }
    waveOffset = WaveReadLaneFirst(waveOffset);

    if (!visible)
        return;
    uint queueIndex = waveOffset + WavePrefixCountBits(visible);
    if (queueIndex >= _constants.queueCapacity)
        return;

    ModelRecord model = _modelRecords[instance.modelIndex];
    uint localLOD = chunk.packedCountRasterAndLOD >> 8u;
    DiagnosticMeshletWork work;
    work.instanceIndex = chunk.instanceIndex;
    work.meshletIndex = meshletIndex;
    work.positionBase = chunk.positionBase;
    work.vertexAttributeBase = chunk.vertexAttributeBase;
    work.meshletVertexIndexBase = chunk.meshletVertexIndexBase;
    work.meshletTriangleBase = chunk.meshletTriangleBase;
    work.colorSeed = MixSeed(MixSeed(MixSeed(MixSeed(0xA511E9B3u, instance.modelIndex), chunk.meshIndex),
                                     localLOD), meshletIndex);
    work.reserved = localLOD;
    ModelMesh mesh = _modelMeshes[model.meshBase + chunk.meshIndex];
    work.positionDecodeOffset = float4(mesh.positionDecodeOffset, 0.0f);
    work.positionDecodeExtent = float4(mesh.positionDecodeExtent, 0.0f);
    StoreWork(rasterClass, queueIndex, work);
}
