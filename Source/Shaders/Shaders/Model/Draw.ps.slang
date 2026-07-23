#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"
#include "Model/ModelAlphaKey.inc.slang"
#include "Model/ModelShared.inc.slang"

struct PSInput
{
    uint4 drawIDInstanceIDTextureDataIDInstanceRefID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;

    uint triangleID : SV_PrimitiveID;
};

struct PSOutput
{
    uint2 visibilityBuffer : SV_Target0;
};

[shader("fragment")]
PSOutput main(PSInput input)
{
    uint textureDataID = input.drawIDInstanceIDTextureDataIDInstanceRefID.z;
    uint instanceRefID = input.drawIDInstanceIDTextureDataIDInstanceRefID.w;

    AlphaKeyDiscard(textureDataID, input.uv01.xy);

    PSOutput output;
    output.visibilityBuffer = PackVisibilityBuffer(ObjectType::ModelOpaque, instanceRefID, input.triangleID);

    return output;
}
