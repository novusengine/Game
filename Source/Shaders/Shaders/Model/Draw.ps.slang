permutation SHADOW_PASS = [0, 1];
#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"
#include "Model/ModelShared.inc.slang"

struct PSInput
{
    uint4 drawIDInstanceIDTextureDataIDInstanceRefID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;

#if !SHADOW_PASS
    uint triangleID : SV_PrimitiveID;
#endif
};

#if !SHADOW_PASS
struct PSOutput
{
    uint2 visibilityBuffer : SV_Target0;
};
#else
#define PSOutput void
#endif

[shader("fragment")]
PSOutput main(PSInput input)
{
    uint drawCallID = input.drawIDInstanceIDTextureDataIDInstanceRefID.x;
    uint instanceID = input.drawIDInstanceIDTextureDataIDInstanceRefID.y;
    uint textureDataID = input.drawIDInstanceIDTextureDataIDInstanceRefID.z;
    uint instanceRefID = input.drawIDInstanceIDTextureDataIDInstanceRefID.w;

    TextureData textureData = LoadModelTextureData(textureDataID);

    for (uint textureUnitIndex = textureData.textureUnitOffset; textureUnitIndex < textureData.textureUnitOffset + textureData.numTextureUnits; textureUnitIndex++)
    {
        ModelTextureUnit textureUnit = _modelTextureUnits[textureUnitIndex];

        uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

        if (blendingMode != 1) // ALPHA KEY
            continue;
        
        uint texture0SamplerIndex = (textureUnit.data1 >> 1) & 0x3;
        uint texture1SamplerIndex = (textureUnit.data1 >> 3) & 0x3;
        uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;

        if (materialType == 0x8000)
            continue;

        float4 texture1 = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].Sample(_samplers[texture0SamplerIndex], input.uv01.xy);
        float4 texture2 = float4(1, 1, 1, 1);

        uint vertexShaderId = materialType & 0xFF;
        if (vertexShaderId > 2)
        {
            // ENV uses generated UVCoords based on camera pos + geometry normal in frame space
            texture2 = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].Sample(_samplers[texture1SamplerIndex], input.uv01.zw);
        }

        // Experimental alphakey discard without shading, if this has issues check github history for cModel.ps.hlsl
        float4 diffuseColor = float4(1, 1, 1, 1);
        // TODO: per-instance diffuseColor

        // TODO : minAlpha is set based on the Pixel Shader Id
        float minAlpha = texture1.a; // min(texture1.a, min(texture2.a, diffuseColor.a));
        if (minAlpha < (128.0 / 255.0))
        {
            discard;
        }
    }

#if !SHADOW_PASS
    PSOutput output;
    output.visibilityBuffer = PackVisibilityBuffer(ObjectType::ModelOpaque, instanceRefID, input.triangleID);

    return output;
#endif
}