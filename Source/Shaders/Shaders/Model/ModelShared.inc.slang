#ifndef MODEL_SHARED_INCLUDED
#define MODEL_SHARED_INCLUDED

#include "DescriptorSet/Model.inc.slang"

#include "Include/Common.inc.slang"

#ifndef GEOMETRY_PASS
#define GEOMETRY_PASS 0
#endif

struct ModelInstanceData
{
    uint modelID;
    uint boneMatrixOffset;
    uint textureTransformMatrixOffset;
    uint modelVertexOffset;
    uint animatedVertexOffset;
    float opacity;
    float highlightIntensity;
    uint padding0;
};

struct PackedTextureData
{
    uint textureUnitOffset;
    uint packed1; // uint16_t numTextureUnits, uint16_t numUnlitTextureUnits
}; // 8 bytes

struct TextureData
{
    uint textureUnitOffset;
    uint numTextureUnits;
    uint numUnlitTextureUnits;
};

struct InstanceRef
{
    uint instanceID;
    uint drawID;
    uint extraID;
    uint padding;
};

uint GetOpaqueInstanceRefID(uint culledInstanceID)
{
    return _opaqueCulledInstanceLookupTable[culledInstanceID];
}

InstanceRef GetOpaqueModelInstanceID(uint instanceRefID)
{
    return _opaqueInstanceRefTable[instanceRefID];
}

TextureData LoadModelTextureData(uint textureDataID)
{
    PackedTextureData packedTextureData = _packedModelTextureDatas[textureDataID];

    TextureData textureData;
    textureData.textureUnitOffset = packedTextureData.textureUnitOffset;
    textureData.numTextureUnits = packedTextureData.packed1 & 0xFFFF;
    textureData.numUnlitTextureUnits = (packedTextureData.packed1 >> 16) && 0xFFFF;

    return textureData;
}

struct PackedModelVertex
{
    uint packed0; // half positionX, half positionY
    uint packed1; // half positionZ, u8 octNormal[2]
    uint packed2; // half uv0X, half uv0Y
    uint packed3; // half uv1X, half uv1Y
    uint packed4; // bone indices (0..4)
    uint packed5; // bone weights (0..4)
}; // 24 bytes

struct ModelVertex
{
    float3 position;

    float4 uv01;
#if !GEOMETRY_PASS
    float3 normal;
#endif

    uint4 boneIndices;
    float4 boneWeights;
};

float3 UnpackModelPosition(PackedModelVertex packedVertex)
{
    float3 position;

    position.x = f16tof32(packedVertex.packed0);
    position.y = f16tof32(packedVertex.packed0 >> 16);
    position.z = f16tof32(packedVertex.packed1);

    return position;
}

float3 UnpackModelNormal(PackedModelVertex packedVertex)
{
    uint x = (packedVertex.packed1 >> 16) & 0xFF;
    uint y = packedVertex.packed1 >> 24;

    float2 octNormal = float2(x, y) / 255.0f;
    return OctNormalDecode(octNormal);
}

float4 UnpackModelUVs(PackedModelVertex packedVertex)
{
    float4 uvs;

    uvs.x = f16tof32(packedVertex.packed2);
    uvs.y = f16tof32(packedVertex.packed2 >> 16);
    uvs.z = f16tof32(packedVertex.packed3);
    uvs.w = f16tof32(packedVertex.packed3 >> 16);

    return uvs;
}

uint4 UnpackModelBoneIndices(PackedModelVertex packedVertex)
{
    uint4 boneIndices;

    boneIndices.x = packedVertex.packed4 & 0xFF;
    boneIndices.y = (packedVertex.packed4 >> 8) & 0xFF;
    boneIndices.z = (packedVertex.packed4 >> 16) & 0xFF;
    boneIndices.w = (packedVertex.packed4 >> 24) & 0xFF;

    return boneIndices;
}

float4 UnpackModelBoneWeights(PackedModelVertex packedVertex)
{
    float4 boneWeights;

    boneWeights.x = (float)(packedVertex.packed5 & 0xFF) / 255.f;
    boneWeights.y = (float)((packedVertex.packed5 >> 8) & 0xFF) / 255.f;
    boneWeights.z = (float)((packedVertex.packed5 >> 16) & 0xFF) / 255.f;
    boneWeights.w = (float)((packedVertex.packed5 >> 24) & 0xFF) / 255.f;

    return boneWeights;
}

ModelVertex LoadModelVertex(uint vertexID)
{
    PackedModelVertex packedVertex = _packedModelVertices[vertexID];

    ModelVertex vertex;
    vertex.position = float3(UnpackModelPosition(packedVertex).x, UnpackModelPosition(packedVertex).y, UnpackModelPosition(packedVertex).z);
    vertex.uv01 = UnpackModelUVs(packedVertex);
    //vertex.uv01 = float4((vertex.uv01.x * 0.5f), (vertex.uv01.y * 0.5f), vertex.uv01.z, vertex.uv01.w);
    //vertex.uv01 = float4(vertex.uv01.x, 1.0f - vertex.uv01.y, vertex.uv01.z, vertex.uv01.w);

#if !GEOMETRY_PASS
    vertex.normal = UnpackModelNormal(packedVertex);
#endif

    vertex.boneIndices = UnpackModelBoneIndices(packedVertex);
    vertex.boneWeights = UnpackModelBoneWeights(packedVertex);

    return vertex;
}

struct PackedAnimatedVertexPosition
{
    uint packed0; // half2 position.xy
    uint packed1; // half position.z, padding
};

void StoreAnimatedVertexPosition(uint animatedVertexID, float3 position)
{
    PackedAnimatedVertexPosition packedAnimatedVertexPosition;
    packedAnimatedVertexPosition.packed0 = f32tof16(position.x);
    packedAnimatedVertexPosition.packed0 |= f32tof16(position.y) << 16;
    packedAnimatedVertexPosition.packed1 = f32tof16(position.z);

    _animatedModelVertexPositions[animatedVertexID] = packedAnimatedVertexPosition;
}

float3 LoadAnimatedVertexPosition(uint animatedVertexID)
{
    PackedAnimatedVertexPosition packedAnimatedVertexPosition = _animatedModelVertexPositions[animatedVertexID];

    float3 position;
    position.x = f16tof32(packedAnimatedVertexPosition.packed0);
    position.y = f16tof32(packedAnimatedVertexPosition.packed0 >> 16);
    position.z = f16tof32(packedAnimatedVertexPosition.packed1);
    return position;
}

float4x4 CalcBoneTransformMatrix(const ModelInstanceData instanceData, ModelVertex vertex)
{
    float4x4 boneTransformMatrix = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

    if (instanceData.boneMatrixOffset != 4294967295)
    {
        boneTransformMatrix = float4x4(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

        [unroll]
        for (int j = 0; j < 4; j++)
        {
            //boneTransformMatrix += mul(vertex.boneWeights[j], _instanceBoneMatrices[instanceData.boneMatrixOffset + vertex.boneIndices[j]]); // Previous line, Slang did not like this
            boneTransformMatrix += _instanceBoneMatrices[instanceData.boneMatrixOffset + vertex.boneIndices[j]] * vertex.boneWeights[j];
        }
    }

    return boneTransformMatrix;
}

struct ModelTextureUnit
{
    uint data1; // (Is Projected Texture (1 bit) + Material Flag (10 bit) + Material Blending Mode (3 bit) + Unused Padding (2 bits)) + Material Type (16 bit)
    uint textureIDs[2];
    uint packedTextureTransformIDs; // u16 textureTransformID[0], u16 textureTransformID[1]
    uint rgba;
    uint padding0;
    uint padding1;
    uint padding2;
};

enum ModelPixelShaderID
{
    Opaque = 0,
    Mod = 1,
    Opaque_Mod = 2,
    Opaque_Mod2x = 3,
    Opaque_Mod2xNA = 4,
    Opaque_Opaque = 5,
    Mod_Mod = 6,
    Mod_Mod2x = 7,
    Mod_Add = 8,
    Mod_Mod2xNA = 9,
    Mod_AddNA = 10,
    Mod_Opaque = 11,
    Opaque_Mod2xNA_Alpha = 12,
    Opaque_AddAlpha = 13,
    Opaque_AddAlpha_Alpha = 14,
    Opaque_Mod2xNA_Alpha_Add = 15,
    Mod_AddAlpha = 16,
    Mod_AddAlpha_Alpha = 17,
    Opaque_Alpha_Alpha = 18,
    Opaque_Mod2xNA_Alpha_3s = 19,
    Opaque_AddAlpha_Wgt = 20,
    Mod_Add_Alpha = 21,
    Opaque_ModNA_Alpha = 22,
    Mod_AddAlpha_Wgt = 23,
    Opaque_Mod_Add_Wgt = 24,
    Opaque_Mod2xNA_Alpha_UnshAlpha = 25,
    Mod_Dual_Crossfade = 26,
    Opaque_Mod2xNA_Alpha_Alpha = 27,
    Mod_Masked_Dual_Crossfade = 28,
    Opaque_Alpha = 29,
    Guild = 30,
    Guild_NoBorder = 31,
    Guild_Opaque = 32,
    Mod_Depth = 33,
    Illum = 34,
    Mod_Mod_Mod_Const = 35,
    UnknownShadeType = 36
};

float4 ShadeModel(uint pixelId, float4 texture1, float4 texture2, out float3 specular)
{
    float4 result = float4(0, 0, 0, 0);
    float4 diffuseColor = float4(1, 1, 1, 1);
    specular = float3(0, 0, 0);

    if (pixelId == ModelPixelShaderID::Opaque)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = texture1.a * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Mod)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = texture2.a * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Mod2x)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a * 2.0f * texture2.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Mod2xNA)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Opaque)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod_Mod)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = texture1.a * texture2.a * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod_Mod2x)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = texture1.a * texture2.a * 2.0f * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod_Add)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = (texture1.a + texture2.a) * diffuseColor.a;

        specular = texture2.rgb;
    }
    else if (pixelId == ModelPixelShaderID::Mod_Mod2xNA)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = texture1.a * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod_AddNA)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = texture1.a * diffuseColor.a;

        specular = texture2.rgb;
    }
    else if (pixelId == ModelPixelShaderID::Mod_Opaque)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = texture1.a * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Mod2xNA_Alpha)
    {
        result.rgb = diffuseColor.rgb * lerp(texture1.rgb * texture2.rgb, texture1.rgb, texture1.aaa);
        result.a = diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_AddAlpha)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a;

        specular = texture2.rgb * texture2.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_AddAlpha_Alpha)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a;

        specular = texture2.rgb * texture2.a * (1.0f - texture1.a);
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Mod2xNA_Alpha_Add)
    {
        result.rgb = diffuseColor.rgb * lerp(texture1.rgb * texture2.rgb, texture1.rgb, texture1.aaa);
        result.a = diffuseColor.a;

        //specular = texture3.rgb * texture3.a * 1.0f;
    }
    else if (pixelId == ModelPixelShaderID::Mod_AddAlpha)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = texture1.a * diffuseColor.a;

        specular = texture2.rgb * texture2.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod_AddAlpha_Alpha)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = (texture1.a + texture2.a * (0.3f * texture2.r + 0.59f * texture2.g + 0.11f * texture2.b)) * diffuseColor.a;

        specular = texture2.rgb * texture2.a * (1.0f - texture1.a);
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Alpha_Alpha)
    {
        result.rgb = diffuseColor.rgb * lerp(lerp(texture1.rgb, texture2.rgb, texture2.aaa), texture1.rgb, texture1.aaa);
        result.a = diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_AddAlpha_Wgt)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = diffuseColor.a;

        specular = texture2.rgb * texture2.a * 1.0f;
    }
    else if (pixelId == ModelPixelShaderID::Mod_Add_Alpha)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = (texture1.a + texture2.a) * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_ModNA_Alpha)
    {
        result.rgb = diffuseColor.rgb * lerp(texture1.rgb * texture2.rgb, texture1.rgb, texture1.aaa);
        result.a = diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod_AddAlpha_Wgt)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = texture1.a * diffuseColor.a;

        specular = texture2.rgb * texture2.a * 1.0f;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Mod_Add_Wgt)
    {
        result.rgb = diffuseColor.rgb * lerp(texture1.rgb, texture2.rgb, texture2.aaa);
        result.a = diffuseColor.a;

        specular = texture1.rgb * texture1.a * 1.0f;
    }
    else if (pixelId == ModelPixelShaderID::Opaque_Alpha)
    {
        result.rgb = diffuseColor.rgb * lerp(texture1.rgb, texture2.rgb, texture2.aaa);
        result.a = diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Guild_NoBorder)
    {
        float4 parameter = float4(1.0f, 1.0f, 1.0f, 1.0f);
        result.rgb = diffuseColor.rgb * texture1.rgb * lerp(parameter.rgb, texture2.rgb * parameter.rgb, texture2.aaa);
        result.a = texture1.a * diffuseColor.a;
    }
    else if (pixelId == ModelPixelShaderID::Mod_Depth)
    {
        float4 parameter = float4(1.0f, 1.0f, 1.0f, 1.0f);

        result.rgb = diffuseColor.rgb * texture1.rgb;
        result.a = texture1.a * diffuseColor.a * parameter.r;
    }
    else if (pixelId == ModelPixelShaderID::Illum)
    {
        // Apparently Unused
        result.rgb = float3(1.0f, 1.0f, 0.0f);
        result.a = 1.0f;
    }
    else if (pixelId == ModelPixelShaderID::UnknownShadeType)
    {
        result.rgb = diffuseColor.rgb * texture1.rgb * texture2.rgb;
        result.a = texture1.a * texture2.a * diffuseColor.a;
    }
    else
    {
        result = float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    // TODO (Implement the following PixelShaderIDs
    /*
        - ModelPixelShaderID::Opaque_Mod2xNA_Alpha_3s (Requires Texture3, we currently do not pass it)
        - ModelPixelShaderID::Opaque_Mod2xNA_Alpha_UnshAlpha (Requires Texture3, we currently do not pass it)
        - ModelPixelShaderID::Mod_Dual_Crossfade (Requires Texture3, we currently do not pass it)
        - ModelPixelShaderID::Opaque_Mod2xNA_Alpha_Alpha (Requires Texture3, we currently do not pass it)
        - ModelPixelShaderID::Mod_Masked_Dual_Crossfade (Requires Texture3, we currently do not pass it)
        - ModelPixelShaderID::Guild (Requires Texture3, we currently do not pass it)
        - ModelPixelShaderID::Guild_NoBorder (Requires Texture3, we currently do not pass it)
        - ModelPixelShaderID::Combiners_Mod_Mod_Mod_Const (Requires Texture3, we currently do not pass it)
    */

    result.rgb = result.rgb;
    return result;
}

float4 BlendModel(uint blendingMode, float4 previousColor, float4 color)
{
    float4 result = float4(1.0f, 0.0f, 1.0f, 1.0f); // MAGENTA, just so it's very visible when something is unimplemented
    float oneMinusSrcAlpha = (1.0f - color.a);

    if (blendingMode == 0) // OPAQUE (ONE, ZERO, ONE, ZERO)
    {
        result = float4(color.rgb, color.a);
    }
    else if (blendingMode == 1) // ALPHA KEY (ONE, ZERO, ONE, ZERO)
    {
        result = color;
    }
    else if (blendingMode == 2) // ALPHA (SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA)
    {
        result.rgb = (color.rgb * color.a) + (previousColor.rgb * oneMinusSrcAlpha);
        result.a = color.a + (previousColor.a * oneMinusSrcAlpha);
    }
    else if (blendingMode == 3) // NO ALPHA ADD (ONE, ONE, ZERO, ONE)
    {
        result.rgb = color.rgb + previousColor.rgb;
        result.a = color.a; // I modified this, it used to be previousColor.a when I thought the blend mode comment made sense
    }
    else if (blendingMode == 4) // ADD (SRC_ALPHA, ONE, ZERO, ONE)
    {
        result.rgb = (color.rgb * color.a) + previousColor.rgb;
        result.a = previousColor.a; // I modified this, it used to be previousColor.a when I thought the blend mode comment made sense
        //result.a = max(color.a, previousColor.a);
    }
    else if (blendingMode == 5) // MOD (DST_COLOR, ZERO, DST_ALPHA, ZERO)
    {
        result = color * previousColor;
    }
    else if (blendingMode == 6) // MOD2X (DST_COLOR, SRC_COLOR, DST_ALPHA, SRC_ALPHA)
    {
        result = (color * previousColor) + (color * previousColor);
    }
    else if (blendingMode == 7) // BLEND ADD (ONE, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA)
    {
        result = color + (previousColor * oneMinusSrcAlpha);
    }

    return result;
}

#endif // MODEL_SHARED_INCLUDED