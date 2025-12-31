permutation TRANSPARENCY = [0, 1];
#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/OIT.inc.slang"
#include "Model/ModelShared.inc.slang"

struct PSInput
{
    float4 position : SV_Position;
    uint textureDataID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
    float3 posViewSpace : TEXCOORD2;
};

struct PSOutput
{
#if TRANSPARENCY
    float4 transparency : SV_Target0;
    float4 transparencyWeight : SV_Target1;
#else
    float4 color : SV_Target0;
#endif
};

[shader("fragment")]
PSOutput main(PSInput input)
{
    TextureData textureData = LoadModelTextureData(input.textureDataID);

    float4 color = float4(0, 0, 0, 0);

    float3 specular = float3(0, 0, 0);

    uint indexThatBecameZero = 0;
    uint blendModeOfThatIndex = 0;

    for (uint textureUnitIndex = textureData.textureUnitOffset; textureUnitIndex < textureData.textureUnitOffset + textureData.numTextureUnits; textureUnitIndex++)
    {
        ModelTextureUnit textureUnit = _modelTextureUnits[textureUnitIndex];

        uint isProjectedTexture = textureUnit.data1 & 0x1;
        uint texture0SamplerIndex = (textureUnit.data1 >> 1) & 0x3;
        uint texture1SamplerIndex = (textureUnit.data1 >> 3) & 0x3;
        uint materialFlags = (textureUnit.data1 >> 1) & 0x3FF;
        uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

        uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;
        uint vertexShaderId = materialType & 0xFF;
        uint pixelShaderId = materialType >> 8;

        if (materialType == 0x8000)
            continue;

        float4 texture0Color = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].Sample(_samplers[texture0SamplerIndex], input.uv01.xy);
        float4 texture1Color = float4(1, 1, 1, 1);

        if (vertexShaderId > 2)
        {
            // ENV uses generated UVCoords based on camera pos + geometry normal in frame space
            texture1Color = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].Sample(_samplers[texture1SamplerIndex], input.uv01.zw);
        }

        float4 shadedColor = ShadeModel(pixelShaderId, texture0Color, texture1Color, specular);
        color = BlendModel(blendingMode, color, shadedColor);

        if (color.a == 0.0f)
        {
            indexThatBecameZero = textureUnitIndex - textureData.textureUnitOffset;
            blendModeOfThatIndex = blendingMode;
        }
    }

    bool isUnlit = textureData.numUnlitTextureUnits;

    //color.rgb = Lighting(color.rgb, float3(0.0f, 0.0f, 0.0f), input.normal, 1.0f, !isUnlit) + specular;
    color = saturate(color);
    
    PSOutput output;
#if TRANSPARENCY
    // Premultiply alpha
    ModelTextureUnit firstTextureUnit = _modelTextureUnits[textureData.textureUnitOffset];
    uint blendingMode = (firstTextureUnit.data1 >> 11) & 0x7;

    float biggestComponent = max(color.x, max(color.y, color.z));
    color.a = biggestComponent * (blendingMode == 4) + color.a * (blendingMode != 4);
    
    float clipSpaceDepth = input.position.z / input.position.w;
    float viewSpaceDepth = input.posViewSpace.z;
    float oitWeight = CalculateOITWeight(color, clipSpaceDepth, viewSpaceDepth);
    
    output.transparency = float4(color.rgb * color.a, color.a) * oitWeight;
    output.transparencyWeight.a = color.a;
#else
    output.color = color;
#endif

    return output;
}