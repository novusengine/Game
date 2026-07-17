permutation EDITOR_PASS = [0, 1];
permutation SVSM_PASS = [0, 1]; // 1 adds per-draw clip-rect distances for the SVSM page render
#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Model.inc.slang"

#include "Include/Common.inc.slang"
#include "Model/ModelShared.inc.slang"
#if SVSM_PASS
#include "Shadows/SVSM.inc.slang"
#endif

struct Constants
{
    uint viewIndex;
#if SVSM_PASS
    uint svsmRectIndex; // Which clip rect this draw renders, SVSM_CLIP_RECT_DISABLED = no clipping
#endif
};

[[vk::push_constant]] Constants _constants;

#if SVSM_PASS
[[vk::binding(0, PER_PASS)]] StructuredBuffer<SVSMData> _svsmData; // Same binding the SVSM pixel shader declares
#endif

struct VSInput
{
    uint vertexID : SV_VulkanVertexID;
    uint culledInstanceID : SV_VulkanInstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
#if (!EDITOR_PASS)
    nointerpolation uint4 drawIDInstanceIDTextureDataIDInstanceRefID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
#endif
#if SVSM_PASS
    float4 clipDistances : SV_ClipDistance; // Fixed-function culls fragments outside the draw's clip rect
#endif
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    uint instanceRefID = _opaqueCulledInstanceLookupTable[input.culledInstanceID];
    InstanceRef instanceRef = _opaqueInstanceRefTable[instanceRefID];
    uint instanceID = instanceRef.instanceID;
    uint drawID = instanceRef.drawID;

    ModelInstanceData instanceData = _modelInstanceDatas[instanceID];

    // Skin this vertex
    ModelVertex vertex = LoadModelVertex(_packedModelVertices, input.vertexID);
    float4x4 boneTransformMatrix = CalcBoneTransformMatrix(_instanceBoneMatrices, instanceData, vertex);
    float4 position = mul(float4(vertex.position, 1.0f), boneTransformMatrix);

    // Save the skinned vertex position (in model-space) if this vertex was animated
    if (instanceData.boneMatrixOffset != 4294967295)
    {
        uint localVertexID = input.vertexID - instanceData.modelVertexOffset; // This gets the local vertex ID relative to the model
        uint animatedVertexID = localVertexID + instanceData.animatedVertexOffset; // This makes it relative to the animated instance

        StoreAnimatedVertexPosition(_animatedModelVertexPositions, animatedVertexID, position.xyz);
    }

    float4x4 instanceMatrix = _modelInstanceMatrices[instanceID];
    position = mul(position, instanceMatrix);

    // Pass data to pixelshader
    VSOutput output;
    output.position = mul(position, _cameras[_constants.viewIndex].worldToClip);

#if SVSM_PASS
    output.clipDistances = SVSMRectClipDistances(position.xyz, _constants.viewIndex - 1, _constants.svsmRectIndex, _cameras[0].eyePosition.xyz, _svsmData);
#endif

#if !EDITOR_PASS
    output.drawIDInstanceIDTextureDataIDInstanceRefID = int4(drawID, instanceID, instanceRef.extraID, instanceRefID);

    float4 UVs = vertex.uv01;

    if (instanceData.textureTransformMatrixOffset != 4294967295)
    {
        TextureData textureData = LoadModelTextureData(_packedModelTextureDatas, instanceRef.extraID);
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

    return output;
}