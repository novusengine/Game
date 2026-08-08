#include "Include/TypedVisibility.inc.slang"

struct PSInput
{
    nointerpolation uint recordID : TEXCOORD0;
    nointerpolation uint materialInstanceIndex : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
    uint triangleID : SV_PrimitiveID;
};

[shader("fragment")]
uint main(PSInput input) : SV_Target0
{
    return PackTypedVisibility(TYPED_VISIBILITY_MODEL, input.recordID, input.triangleID);
}
