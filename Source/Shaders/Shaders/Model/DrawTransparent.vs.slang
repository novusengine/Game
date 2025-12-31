
#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Model/ModelShared.inc.slang"

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _culledInstanceLookupTable; // One uint per instance, contains instanceRefID of what survives culling, and thus can get reordered
[[vk::binding(1, PER_PASS)]] StructuredBuffer<InstanceRef> _instanceRefTable;

struct VSInput
{
    uint vertexID : SV_VulkanVertexID;
    uint culledInstanceID : SV_VulkanInstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;

    nointerpolation uint textureDataID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
    float4 posViewSpaceAndOpacity : TEXCOORD2;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    uint instanceRefID = _culledInstanceLookupTable[input.culledInstanceID];
    InstanceRef instanceRef = _instanceRefTable[instanceRefID];
    uint instanceID = instanceRef.instanceID;
    uint drawID = instanceRef.drawID;
    uint textureDataID = instanceRef.extraID;

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

    float4x4 instanceMatrix = _modelInstanceMatrices[instanceID];
    position = mul(position, instanceMatrix);

    float4 UVs = vertex.uv01;

    if (instanceData.textureTransformMatrixOffset != 4294967295)
    {
        TextureData textureData = LoadModelTextureData(textureDataID);
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

    // Pass data to pixelshader
    VSOutput output;
    output.position = mul(position, _cameras[0].worldToClip);
    output.textureDataID = textureDataID;
    output.uv01 = UVs;
    output.posViewSpaceAndOpacity = float4(mul(position, _cameras[0].worldToView).xyz, instanceData.opacity);

    return output;
}