permutation SHADOW_PASS = [0, 1];
permutation SUPPORTS_EXTENDED_TEXTURES = [0, 1];
#define GEOMETRY_PASS 1

#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Model/Shared.inc.hlsl"
#include "Include/VisibilityBuffers.inc.hlsl"

[[vk::binding(10, MODEL)]] SamplerState _sampler;

struct PSInput
{
    uint drawID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;

#if !SHADOW_PASS
    float3 modelPosition : TEXCOORD2;
    uint triangleID : SV_PrimitiveID;
#endif
};

#if !SHADOW_PASS
struct PSOutput
{
    uint4 visibilityBuffer : SV_Target0;
};
#else
#define PSOutput void
#endif

PSOutput main(PSInput input)
{
    ModelDrawCallData drawCallData = LoadModelDrawCallData(input.drawID);

    for (uint textureUnitIndex = drawCallData.textureUnitOffset; textureUnitIndex < drawCallData.textureUnitOffset + drawCallData.numTextureUnits; textureUnitIndex++)
    {
        ModelTextureUnit textureUnit = _modelTextureUnits[textureUnitIndex];

        uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

        if (blendingMode != 1) // ALPHA KEY
            continue;

        uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;

        if (materialType == 0x8000)
            continue;

        float4 texture1 = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].Sample(_sampler, input.uv01.xy);
        float4 texture2 = float4(1, 1, 1, 1);

        uint vertexShaderId = materialType & 0xFF;
        if (vertexShaderId > 2)
        {
            // ENV uses generated UVCoords based on camera pos + geometry normal in frame space
            texture2 = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].Sample(_sampler, input.uv01.zw);
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
    ModelInstanceData instanceData = _modelInstanceDatas[drawCallData.instanceID];
    float4x4 instanceMatrix = _modelInstanceMatrices[drawCallData.instanceID];

    // Get the VertexIDs of the triangle we're in
    Draw draw = _modelDraws[input.drawID];
    uint3 vertexIDs = GetVertexIDs(input.triangleID, draw, _modelIndices);

    // Load the vertices
    ModelVertex vertices[3];

    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        vertices[i] = LoadModelVertex(vertexIDs[i]);

        // Load the skinned vertex position (in model-space) if this vertex was animated
        if (instanceData.boneMatrixOffset != 4294967295)
        {
            uint localVertexID = vertexIDs[i] - instanceData.modelVertexOffset; // This gets the local vertex ID relative to the model
            uint animatedVertexID = localVertexID + instanceData.animatedVertexOffset; // This makes it relative to the animated instance

            vertices[i].position = LoadAnimatedVertexPosition(animatedVertexID);
        }

        vertices[i].position = mul(float4(vertices[i].position, 1.0f), instanceMatrix).xyz;
    }

    // Calculate Barycentrics
    float2 barycentrics = NBLCalculateBarycentrics(input.modelPosition, float3x3(vertices[0].position.xyz, vertices[1].position.xyz, vertices[2].position.xyz));

    float2 ddxBarycentrics = ddx(barycentrics);
    float2 ddyBarycentrics = ddy(barycentrics);

    PSOutput output;
    output.visibilityBuffer = PackVisibilityBuffer(ObjectType::ModelOpaque, input.drawID, input.triangleID, barycentrics, ddxBarycentrics, ddyBarycentrics);

    return output;
#endif
}