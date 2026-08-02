#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"
#include "Terrain/TerrainShared.inc.slang"

struct PSInput
{
    uint triangleID : SV_PrimitiveID;
    uint instanceID : TEXCOORD0;
};

struct PSOutput
{
    uint visibilityBuffer : SV_Target0;
};

[shader("fragment")]
PSOutput main(PSInput input)
{
    PSOutput output;

    output.visibilityBuffer = PackTerrainVisibility(input.instanceID, input.triangleID);
    return output;
}
