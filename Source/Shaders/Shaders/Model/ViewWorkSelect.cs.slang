#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint viewIndex;
    uint inputCount;
    uint selectionCapacity;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ViewInstanceInput> _viewInputs;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelInstances;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelRecord> _modelRecords;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<ViewInstanceInput> _selectedInputs0;
[[vk::binding(4, PER_PASS)]] RWStructuredBuffer<ViewInstanceInput> _selectedInputs1;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(6, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;

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

void StoreSelectedInput(uint index, ViewInstanceInput input)
{
    if (_constants.resourceIndex == 0u) _selectedInputs0[index] = input;
    else _selectedInputs1[index] = input;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= _constants.inputCount)
        return;

    ViewInstanceInput input = _viewInputs[dispatchThreadID.x];
    uint instanceCount;
    uint instanceStride;
    _modelInstances.GetDimensions(instanceCount, instanceStride);
    AddStat(MODEL_WORK_STAT_TESTED_INSTANCES, 1u);
    if (input.instanceIndex == 0xFFFFFFFFu || input.instanceIndex >= instanceCount)
    {
        AddStat(MODEL_WORK_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    ModelInstanceRecord instance = _modelInstances[input.instanceIndex];
    if ((instance.flags & MODEL_INSTANCE_VISIBLE) == 0u ||
        (instance.flags & MODEL_INSTANCE_FORCE_TRANSPARENT) != 0u)
    {
        AddStat(MODEL_WORK_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    uint modelCount;
    uint modelStride;
    _modelRecords.GetDimensions(modelCount, modelStride);
    if (instance.modelIndex >= modelCount)
    {
        AddStat(MODEL_WORK_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    ModelRecord model = _modelRecords[instance.modelIndex];
    Camera camera = _cameras[_constants.viewIndex];
    float4 worldBounds = TransformSphere(model.bounds.center, model.bounds.sphereRadius, instance.currentWorld);
    if (instance.deformationHandle == 0xFFFFFFFFu && !ModelSphereInsideFrustum(camera, worldBounds))
    {
        AddStat(MODEL_WORK_STAT_REJECTED_INSTANCES, 1u);
        return;
    }

    uint selectedIndex;
    AddStat(MODEL_WORK_STAT_SELECTED_INSTANCES, 1u, selectedIndex);
    if (selectedIndex < _constants.selectionCapacity)
        StoreSelectedInput(selectedIndex, input);
}
