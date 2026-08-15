#include "DescriptorSet/Global.inc.slang"
#include "Include/PyramidCulling.inc.slang"
#include "Model/ModelTransparentWork.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint viewIndex;
    uint resourceIndex;
    uint binCapacity;
    uint enableConeCulling;
    uint enableOcclusionCulling;
    uint viewportWidth;
    uint viewportHeight;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelTransparentChunk> _transparentChunks0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelTransparentChunk> _transparentChunks1;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _transparentCullInstances;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelRecord> _transparentCullModels;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelMeshlet> _transparentCullMeshlets;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<ModelVisibilityRecord> _transparentRecords0;
[[vk::binding(6, PER_PASS)]] RWStructuredBuffer<ModelVisibilityRecord> _transparentRecords1;
[[vk::binding(7, PER_PASS)]] RWStructuredBuffer<uint> _transparentStats0;
[[vk::binding(8, PER_PASS)]] RWStructuredBuffer<uint> _transparentStats1;
[[vk::binding(9, PER_PASS)]] Texture2D<float> _transparentDepthPyramid;
[[vk::binding(10, PER_PASS)]] SamplerState _transparentDepthSampler;

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

[shader("compute")]
[numthreads(MODEL_CHUNK_MESHLET_COUNT, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    ModelTransparentChunk chunk = _constants.resourceIndex == 0u ? _transparentChunks0[groupID.x] : _transparentChunks1[groupID.x];
    uint meshletCount = chunk.packedCountAndLOD & MODEL_TRANSPARENT_COUNT_MASK;
    if (groupThreadID.x == 0u)
        AddStat(MODEL_TRANSPARENT_STAT_TESTED_MESHLETS, meshletCount);

    bool visible = groupThreadID.x < meshletCount;
    ModelMeshlet meshlet;
    if (visible)
    {
        ModelInstanceRecord instance = _transparentCullInstances[chunk.instanceIndex];
        ModelRecord model = _transparentCullModels[instance.modelIndex];
        meshlet = _transparentCullMeshlets[chunk.meshletBase + groupThreadID.x];
        AddStat(MODEL_TRANSPARENT_STAT_TESTED_TRIANGLES, ModelMeshletTriangleCount(meshlet));
        float4 sphere = TransformSphere(meshlet.boundsCenter, meshlet.boundsRadius, instance.currentWorld);
        Camera camera = _cameras[_constants.viewIndex];
        if (!ModelSphereInsideFrustum(camera, sphere))
        {
            AddStat(MODEL_TRANSPARENT_STAT_REJECTED_FRUSTUM_MESHLETS, 1u);
            visible = false;
        }
        else if (_constants.enableConeCulling != 0u && (chunk.binIndex & 1u) == 0u &&
                 ModelConeBackfacing(meshlet.packedNormalCone, meshlet.boundsCenter, meshlet.boundsRadius,
                                     instance.currentWorld, camera.cullingEyePosition.xyz))
        {
            AddStat(MODEL_TRANSPARENT_STAT_REJECTED_CONE_MESHLETS, 1u);
            visible = false;
        }
        else if (_constants.enableOcclusionCulling != 0u)
        {
            float3 boundsMin = sphere.xyz - sphere.www;
            float3 boundsMax = sphere.xyz + sphere.www;
            if (!IsIntersectingNearZ(boundsMin, boundsMax, camera.worldToClip) &&
                !IsVisible(boundsMin, boundsMax, camera.eyePosition.xyz, _transparentDepthPyramid,
                           _transparentDepthSampler, camera.worldToClip,
                           uint2(_constants.viewportWidth, _constants.viewportHeight)))
            {
                AddStat(MODEL_TRANSPARENT_STAT_REJECTED_OCCLUSION_MESHLETS, 1u);
                visible = false;
            }
        }
    }

    uint visibleCount = WaveActiveCountBits(visible);
    uint queueOffset = 0u;
    uint committedCount = 0u;
    if (WaveIsFirstLane())
    {
        AddStat(MODEL_TRANSPARENT_STAT_QUEUE_COUNTS + chunk.binIndex, visibleCount, queueOffset);
        committedCount = min(visibleCount, _constants.binCapacity - min(queueOffset, _constants.binCapacity));
        if (committedCount < visibleCount)
            AddStat(MODEL_TRANSPARENT_STAT_QUEUE_OVERFLOWS, visibleCount - committedCount);
    }
    queueOffset = WaveReadLaneFirst(queueOffset);
    committedCount = WaveReadLaneFirst(committedCount);
    if (!visible)
        return;
    uint waveIndex = WavePrefixCountBits(visible);
    if (waveIndex >= committedCount)
        return;

    uint localLOD = (chunk.packedCountAndLOD >> MODEL_TRANSPARENT_LOD_SHIFT) & MODEL_TRANSPARENT_LOD_MASK;
    ModelInstanceRecord instance = _transparentCullInstances[chunk.instanceIndex];
    ModelRecord model = _transparentCullModels[instance.modelIndex];
    uint localMeshletIndex = chunk.meshletBase + groupThreadID.x - model.meshletBase;
    ModelVisibilityRecord record = PackModelVisibilityRecord(chunk.instanceIndex, chunk.meshIndex, localLOD,
                                                              chunk.submeshIndex, localMeshletIndex);
    uint destination = chunk.binIndex * _constants.binCapacity + queueOffset + waveIndex;
    if (_constants.resourceIndex == 0u) _transparentRecords0[destination] = record;
    else _transparentRecords1[destination] = record;
    AddStat(MODEL_TRANSPARENT_STAT_COMMITTED_TRIANGLES, ModelMeshletTriangleCount(meshlet));
}
