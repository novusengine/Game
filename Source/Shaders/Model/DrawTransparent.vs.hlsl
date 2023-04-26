
#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Model/Shared.inc.hlsl"

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;

    nointerpolation uint drawCallID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
    float3 normal : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    uint drawCallID = input.instanceID;
    ModelVertex vertex = LoadModelVertex(input.vertexID);

    ModelDrawCallData drawCallData = LoadModelDrawCallData(drawCallID);
    ModelInstanceData instanceData = _modelInstanceDatas[drawCallData.instanceID];
    float4x4 instanceMatrix = _modelInstanceMatrices[drawCallData.instanceID];

    // Skin this vertex
    //float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertex);

    /* TODO: Animation
    float4 position = mul(float4(vertex.position, 1.0f), boneTransformMatrix);

    // Save the skinned vertex position (in model-space) if this vertex was animated
    /if (instanceData.boneDeformOffset != 4294967295)
    {
        uint localVertexID = input.vertexID - instanceData.modelVertexOffset; // This gets the local vertex ID relative to the model
        uint animatedVertexID = localVertexID + instanceData.animatedVertexOffset; // This makes it relative to the animated instance

        StoreAnimatedVertexPosition(animatedVertexID, position.xyz);
    }*/

    float4 position = float4(vertex.position, 1.0f);
    position = mul(position, instanceMatrix);

    float4 UVs = vertex.uv01;
    /* TODO: Texture animations
    if (instanceData.textureTransformDeformOffset != 4294967295)
    {
        CModelDrawCallData drawCallData = LoadCModelDrawCallData(drawCallID);
        uint numTextureUnits = drawCallData.numTextureUnits;

        if (numTextureUnits > 0)
        {
            uint textureUnitIndex = drawCallData.textureUnitOffset;
            CModelTextureUnit textureUnit = _cModelTextureUnits[textureUnitIndex];
            uint textureTransformID1 = textureUnit.packedTextureTransformIDs & 0xFFFF;
            uint textureTransformID2 = (textureUnit.packedTextureTransformIDs >> 16) & 0xFFFF;

            if (textureTransformID1 != 65535)
            {
                const float4x4 textureTransform1Matrix = _cModelAnimationBoneDeformMatrices[instanceData.textureTransformDeformOffset + textureTransformID1];
                UVs.xy = mul(float4(UVs.xy, 0.0f, 1.0f), textureTransform1Matrix).xy;
            }

            if (textureTransformID2 != 65535)
            {
                const float4x4 textureTransform2Matrix = _cModelAnimationBoneDeformMatrices[instanceData.textureTransformDeformOffset + textureTransformID2];
                UVs.zw = mul(float4(UVs.zw, 0.0f, 1.0f), textureTransform2Matrix).xy;
            }
        }
    }*/

    // Pass data to pixelshader
    VSOutput output;
    output.position = mul(position, _cameras[0].worldToClip);
    output.drawCallID = drawCallID;
    output.uv01 = UVs;
    output.normal = mul(vertex.normal, (float3x3)instanceMatrix);

    return output;
}