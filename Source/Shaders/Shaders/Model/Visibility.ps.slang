#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelRasterQueue.inc.slang"

struct PSInput
{
    nointerpolation uint recordEntry : TEXCOORD0;
    nointerpolation uint materialInstanceIndex : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
    uint triangleID : SV_PrimitiveID;
};

struct Constants
{
    uint queueIndex;
    uint viewIndex;
    uint resourceIndex;
    uint previousResource;
    uint geometryQueue;
};
[[vk::push_constant]] Constants _constants;

[shader("fragment")]
uint main(PSInput input) : SV_Target0
{
    uint recordID = ModelRasterQueueRecordID(input.recordEntry);
    uint resolveClass = ModelRasterQueueResolveClass(input.recordEntry);
    return PackTypedVisibility(ModelVisibilityGeometryType(resolveClass, _constants.previousResource != 0u),
                               recordID, input.triangleID);
}
