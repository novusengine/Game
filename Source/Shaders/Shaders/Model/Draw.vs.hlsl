permutation EDITOR_PASS = [0, 1];
permutation SHADOW_PASS = [0, 1];
permutation SUPPORTS_EXTENDED_TEXTURES = [0, 1];
#define GEOMETRY_PASS 1

#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Model/ModelShared.inc.hlsl"

struct Constants
{
    uint viewIndex;
};

[[vk::push_constant]] Constants _constants;

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint culledInstanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
#if (!EDITOR_PASS)
    nointerpolation uint4 drawIDInstanceIDTextureDataIDInstanceRefID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
#endif
#if (!EDITOR_PASS) && (!SHADOW_PASS)
    float3 modelPosition : TEXCOORD2;
#endif
};

VSOutput main(VSInput input)
{
    uint instanceRefID = GetInstanceRefID(input.culledInstanceID);
    InstanceRef instanceRef = GetModelInstanceID(instanceRefID);
    uint instanceID = instanceRef.instanceID;
    uint drawID = instanceRef.drawID;

    ModelInstanceData instanceData = _modelInstanceDatas[instanceID];

    // Skin this vertex
    ModelVertex vertex = LoadModelVertex(input.vertexID);
    float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertex);
    float4 position = mul(float4(vertex.position, 1.0f), boneTransformMatrix);

    // Save the skinned vertex position (in model-space) if this vertex was animated
    if (instanceData.boneMatrixOffset != 4294967295)
    {
        uint localVertexID = input.vertexID - instanceData.modelVertexOffset; // This gets the local vertex ID relative to the model
        uint animatedVertexID = localVertexID + instanceData.animatedVertexOffset; // This makes it relative to the animated instance

        StoreAnimatedVertexPosition(animatedVertexID, position.xyz);
    }
    float3 modelPosition = position.xyz;

    float4x4 instanceMatrix = _modelInstanceMatrices[instanceID];
    position = mul(position, instanceMatrix);

    // Pass data to pixelshader
    VSOutput output;
    output.position = mul(position, _cameras[_constants.viewIndex].worldToClip);

#if !EDITOR_PASS
    output.drawIDInstanceIDTextureDataIDInstanceRefID = int4(drawID, instanceID, instanceRef.extraID, instanceRefID);

    float4 UVs = vertex.uv01;

    if (instanceData.textureTransformMatrixOffset != 4294967295)
    {
        TextureData textureData = LoadModelTextureData(instanceRef.extraID);
        uint numTextureUnits = textureData.numTextureUnits;

        if (numTextureUnits > 0)
        {
            uint textureUnitIndex = textureData.textureUnitOffset;
            ModelTextureUnit textureUnit = _modelTextureUnits[textureUnitIndex];
            uint textureTransformID1 = textureUnit.packedTextureTransformIDs & 0xFFFF;
            uint textureTransformID2 = (textureUnit.packedTextureTransformIDs >> 16) & 0xFFFF;

            if (textureTransformID1 != 65535)
            {
                const float4x4 textureTransform1Matrix = _instanceTextureTransformMatrices[instanceData.textureTransformMatrixOffset + textureTransformID1];
                UVs.xy = mul(float4(UVs.xy, 0.0f, 1.0f), textureTransform1Matrix).xy;
            }

            if (textureTransformID2 != 65535)
            {
                const float4x4 textureTransform2Matrix = _instanceTextureTransformMatrices[instanceData.textureTransformMatrixOffset + textureTransformID2];
                UVs.zw = mul(float4(UVs.zw, 0.0f, 1.0f), textureTransform2Matrix).xy;
            }
        }
    }

    output.uv01 = UVs;
#endif
#if (!EDITOR_PASS) && (!SHADOW_PASS)
    output.modelPosition = modelPosition.xyz;
#endif

    return output;
}