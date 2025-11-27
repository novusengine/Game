permutation SUPPORTS_EXTENDED_TEXTURES = [0, 1];
#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.hlsl"

#include "Include/Common.inc.hlsl"
#include "Include/VisibilityBuffers.inc.hlsl"
#include "Terrain/TerrainShared.inc.hlsl"

struct PSInput
{
    uint triangleID : SV_PrimitiveID;
    uint instanceID : TEXCOORD0;
};

struct PSOutput
{
    uint2 visibilityBuffer : SV_Target0;
};

[shader("fragment")]
PSOutput main(PSInput input)
{
    PSOutput output;

    output.visibilityBuffer = PackVisibilityBuffer(ObjectType::Terrain, input.instanceID, input.triangleID);
    return output;
}