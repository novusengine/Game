#ifndef VISIBILITYBUFFERS_INCLUDED
#define VISIBILITYBUFFERS_INCLUDED

#include "Terrain/TerrainShared.inc.hlsl"
#include "Model/ModelShared.inc.hlsl"
#include "globalData.inc.hlsl"

#if GEOMETRY_PASS
uint4 PackVisibilityBuffer(uint typeID, uint instanceID, uint triangleID, float2 barycentrics, float2 ddxBarycentrics, float2 ddyBarycentrics)
{
    // VisibilityBuffer is 4 uints packed like this:
    // X
    //      TriangleID 8 bits (out of 16), we had to split this to fit
    //      InstanceID 20 bits, different types can have the same InstanceID
    //      TypeID 4 bits
    // Y
    //      TriangleID 8 bits (out of 16), the remainder
    //      Barycentrics.x 12 bits
    //      Barycentrics.y 12 bits
    // Z
    //      ddx(bary).x 16 bits
    //      ddx(bary).y 16 bits
    // W
    //      ddy(bary).x 16 bits
    //      ddy(bary).y 16 bits

    uint2 twelveBitUnormBarycentrics = barycentrics * 4096;

    uint4 packedVisBuffer;
    // X
    packedVisBuffer.x = (triangleID & 0xFF);
    packedVisBuffer.x |= instanceID << 8;
    packedVisBuffer.x |= typeID << 28;
    // Y
    packedVisBuffer.y = (triangleID >> 8);
    packedVisBuffer.y |= twelveBitUnormBarycentrics.x << 8;
    packedVisBuffer.y |= twelveBitUnormBarycentrics.y << 20;
    // Z
    packedVisBuffer.z = f32tof16(ddxBarycentrics.x);
    packedVisBuffer.z |= f32tof16(ddxBarycentrics.y) << 16;
    // W
    packedVisBuffer.w = f32tof16(ddyBarycentrics.x);
    packedVisBuffer.w |= f32tof16(ddyBarycentrics.y) << 16;

    return packedVisBuffer;
}

float2 _NBLCalculateBarycentrics(in float3 positionRelativeToV0, in float2x3 edges)
{
    const float e0_2 = dot(edges[0], edges[0]);
    const float e0e1 = dot(edges[0], edges[1]);
    const float e1_2 = dot(edges[1], edges[1]);

    const float qe0 = dot(positionRelativeToV0, edges[0]);
    const float qe1 = dot(positionRelativeToV0, edges[1]);
    const float2 protoBary = float2(qe0 * e1_2 - qe1 * e0e1, qe1 * e0_2 - qe0 * e0e1);

    const float rcp_dep = 1.f / (e0_2 * e1_2 - e0e1 * e0e1);
    return protoBary * rcp_dep;
}

float2 NBLCalculateBarycentrics(in float3 pointPosition, in float3x3 vertexPositions)
{
    return _NBLCalculateBarycentrics(pointPosition - vertexPositions[2], float2x3(vertexPositions[0] - vertexPositions[2], vertexPositions[1] - vertexPositions[2]));
}

#else // GEOMETRY_PASS

[[vk::binding(2, PER_PASS)]] Texture2D<uint4> _visibilityBuffer;

struct Barycentrics
{
    float2 bary;
    float2 ddxBary;
    float2 ddyBary;
};

struct VisibilityBuffer
{
    uint typeID;
    uint instanceID;
    uint padding;
    uint triangleID;
    Barycentrics barycentrics;
};

uint4 LoadVisibilityBuffer(uint2 pixelPos)
{
    return _visibilityBuffer[pixelPos];
}

const VisibilityBuffer UnpackVisibilityBuffer(uint4 data)
{
    // VisibilityBuffer is 4 uints packed like this:
    // X
    //      TriangleID 8 bits (out of 16), we had to split this to fit
    //      InstanceID 20 bits, different types can have the same InstanceID
    //      TypeID 4 bits
    // Y
    //      TriangleID 8 bits (out of 16), the remainder
    //      Barycentrics.x 12 bits
    //      Barycentrics.y 12 bits
    // Z
    //      ddx(bary).x 16 bits
    //      ddx(bary).y 16 bits
    // W
    //      ddy(bary).x 16 bits
    //      ddy(bary).y 16 bits

    VisibilityBuffer vBuffer;
    vBuffer.typeID = data.x >> 28;
    vBuffer.instanceID = (data.x & 0x0FFFFF00) >> 8;

    vBuffer.triangleID = (data.x & 0xFF) | ((data.y & 0xFF) << 8);

    uint barycentrics = (data.y >> 8);
    vBuffer.barycentrics.bary.x = float(barycentrics & 0xFFF) / 4095.0f;
    vBuffer.barycentrics.bary.y = float(barycentrics >> 12) / 4095.0f;

    vBuffer.barycentrics.ddxBary.x = f16tof32(data.z);
    vBuffer.barycentrics.ddxBary.y = f16tof32(data.z >> 16);

    vBuffer.barycentrics.ddyBary.x = f16tof32(data.w);
    vBuffer.barycentrics.ddyBary.y = f16tof32(data.w >> 16);

    return vBuffer;
}

uint GetObjectID(uint typeID, uint instanceID)
{
    if (typeID == ObjectType::Terrain)
    {
        InstanceData debugCellInstance = _instanceDatas[instanceID];

        return debugCellInstance.globalCellID;
    }

    return instanceID;
}

float InterpolateWithBarycentrics(Barycentrics barycentrics, float v0, float v1, float v2)
{
    float3 bary;
    bary.xy = barycentrics.bary;
    bary.z = 1.0 - bary.x - bary.y;

    float3 mergedV = float3(v0, v1, v2);
    return dot(bary, mergedV);
}

float InterpolateVertexAttribute(Barycentrics barycentrics, float v0, float v1, float v2)
{
    float2x1 dVdBary;
    dVdBary[0] = v0 - v2;
    dVdBary[1] = v1 - v2;

    return mul(barycentrics.bary, dVdBary) + v2;
}

float2 InterpolateVertexAttribute(Barycentrics barycentrics, float2 v0, float2 v1, float2 v2)
{
    float2x2 dVdBary;
    dVdBary[0] = v0 - v2;
    dVdBary[1] = v1 - v2;

    return mul(barycentrics.bary, dVdBary) + v2;
}

float3 InterpolateVertexAttribute(Barycentrics barycentrics, float3 v0, float3 v1, float3 v2)
{
    float2x3 dVdBary;
    dVdBary[0] = v0 - v2;
    dVdBary[1] = v1 - v2;

    return mul(barycentrics.bary, dVdBary) + v2;
}

float4 InterpolateVertexAttribute(Barycentrics barycentrics, float4 v0, float4 v1, float4 v2)
{
    float2x4 dVdBary;
    dVdBary[0] = v0 - v2;
    dVdBary[1] = v1 - v2;

    return mul(barycentrics.bary, dVdBary) + v2;
}

struct FullBary2
{
    float2 value;
    float2 ddx;
    float2 ddy;
};

FullBary2 CalcFullBary2(Barycentrics barycentrics, float2 v0, float2 v1, float2 v2)
{
    float2x2 dVdBary;
    dVdBary[0] = v0 - v2;
    dVdBary[1] = v1 - v2;

    FullBary2 result;
    result.value = mul(barycentrics.bary, dVdBary) + v2;

    float2x2 dVdScreen = mul(float2x2(barycentrics.ddxBary, barycentrics.ddyBary), dVdBary);
    result.ddx = dVdScreen[0];
    result.ddy = dVdScreen[1];

    return result;
}

struct FullBary3
{
    float3 value;
    float3 ddx;
    float3 ddy;
};

FullBary3 CalcFullBary3(Barycentrics barycentrics, float3 v0, float3 v1, float3 v2)
{
    float2x3 dVdBary;
    dVdBary[0] = v0 - v2;
    dVdBary[1] = v1 - v2;

    FullBary3 result;
    result.value = mul(barycentrics.bary, dVdBary) + v2;

    float2x3 dVdScreen = mul(float2x2(barycentrics.ddxBary, barycentrics.ddyBary), dVdBary);
    result.ddx = dVdScreen[0];
    result.ddy = dVdScreen[1];

    return result;
}

struct PixelVertexData
{
    uint typeID;
    uint drawCallID; // Same as instanceID for terrain
    FullBary2 uv0;
    FullBary2 uv1; // Only used for models, for terrain it's a copy of uv0
    float3 color; // Only used for terrain, for models it's hardcoded to white
    float3 worldNormal;
    float3 worldPos;
    float4 viewPos;
    float2 pixelPos;
};

PixelVertexData GetPixelVertexDataTerrain(const uint2 pixelPos, const VisibilityBuffer vBuffer, const uint cameraIndex)
{
    InstanceData cellInstance = _instanceDatas[vBuffer.instanceID];
    uint globalCellID = cellInstance.globalCellID;

    // Find the cell and chunk
    uint globalVertexOffset = globalCellID * NUM_VERTICES_PER_CELL;
    uint3 localVertexIDs = GetLocalTerrainVertexIDs(vBuffer.triangleID);

    const uint cellID = cellInstance.packedChunkCellID & 0xFFFF;
    const uint chunkID = cellInstance.packedChunkCellID >> 16;

    // Get vertices
    TerrainVertex vertices[3];

    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        vertices[i] = LoadTerrainVertex(chunkID, cellID, globalVertexOffset, localVertexIDs[i]);
    }

    // Interpolate the pixels vertex attributes
    PixelVertexData result;
    result.typeID = vBuffer.typeID;
    result.drawCallID = vBuffer.instanceID;

    // UV
    result.uv0 = CalcFullBary2(vBuffer.barycentrics, vertices[0].uv, vertices[1].uv, vertices[2].uv); // [0..8] This is correct for terrain color textures
    result.uv1 = result.uv0;

    // Color
    result.color = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].color, vertices[1].color, vertices[2].color);

    // Normal
    result.worldNormal = normalize(InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].normal, vertices[1].normal, vertices[2].normal));

    // Position
    result.worldPos = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].position, vertices[1].position, vertices[2].position);
    result.viewPos = mul(float4(result.worldPos, 1.0f), _cameras[cameraIndex].worldToView);
    result.pixelPos = pixelPos;

    return result;
}

PixelVertexData GetPixelVertexDataModel(const uint2 pixelPos, const VisibilityBuffer vBuffer, const uint cameraIndex)
{
    InstanceRef instanceRef = GetModelInstanceID(vBuffer.instanceID);
    uint instanceID = instanceRef.instanceID;
    uint drawID = instanceRef.drawID;

    ModelInstanceData instanceData = _modelInstanceDatas[instanceID];

    // Get the VertexIDs of the triangle we're in
    IndexedDraw draw = _modelDraws[drawID];
    uint3 vertexIDs = GetVertexIDs(vBuffer.triangleID, draw, _modelIndices);

    // Get Vertices
    ModelVertex vertices[3];

    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        vertices[i] = LoadModelVertex(vertexIDs[i]);

        // Animate the vertex normal if we need to
        if (instanceData.boneMatrixOffset != 4294967295)
        {
            // Calculate bone transform matrix
            float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertices[i]);

            vertices[i].position = mul(vertices[i].position, (float3x3)boneTransformMatrix);
            vertices[i].normal = mul(vertices[i].normal, (float3x3)boneTransformMatrix);
        }
    }

    // Interpolate the pixels vertex attributes
    PixelVertexData result;
    result.typeID = vBuffer.typeID;
    result.drawCallID = instanceRef.drawID;

    // UV
    result.uv0 = CalcFullBary2(vBuffer.barycentrics, vertices[0].uv01.xy, vertices[1].uv01.xy, vertices[2].uv01.xy);
    result.uv1 = CalcFullBary2(vBuffer.barycentrics, vertices[0].uv01.zw, vertices[1].uv01.zw, vertices[2].uv01.zw);

    // Models don't have vertex colors
    result.color = float3(1, 1, 1);

    float4x4 instanceMatrix = _modelInstanceMatrices[instanceID];

    // Normal
    float3 vertexNormal = normalize(InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].normal, vertices[1].normal, vertices[2].normal));
    result.worldNormal = normalize(mul(vertexNormal, (float3x3)instanceMatrix)); // Convert to world space

    // World Position
    float3 pixelVertexPosition = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].position, vertices[1].position, vertices[2].position);
    result.worldPos = mul(float4(pixelVertexPosition, 1.0f), instanceMatrix).xyz; // Convert to world space
    result.viewPos = mul(float4(result.worldPos, 1.0f), _cameras[cameraIndex].worldToView);
    result.pixelPos = pixelPos;

    return result;
}

PixelVertexData GetPixelVertexData(const uint2 pixelPos, const VisibilityBuffer vBuffer, uint cameraIndex)
{
    if (vBuffer.typeID == ObjectType::Terrain)
    {
        return GetPixelVertexDataTerrain(pixelPos, vBuffer, cameraIndex);
    }
    else if (vBuffer.typeID == ObjectType::ModelOpaque)
    {
        return GetPixelVertexDataModel(pixelPos, vBuffer, cameraIndex);
    }

    PixelVertexData result;
    result.typeID = vBuffer.typeID;
    result.drawCallID = 0;

    result.uv0.value = float2(0, 0);
    result.uv0.ddx = float2(0, 0);
    result.uv0.ddy = float2(0, 0);

    result.uv1.value = float2(0, 0);
    result.uv1.ddx = float2(0, 0);
    result.uv1.ddy = float2(0, 0);

    result.color = float3(1, 1, 1);
    result.worldNormal = float3(0, 1, 0);
    result.worldPos = float3(0, 0, 0);
    result.viewPos = float4(0, 0, 0, 1);
    result.pixelPos = pixelPos;

    return result;
}
#endif // GEOMETRY_PASS

#define RED_SEED 3
#define GREEN_SEED 5
#define BLUE_SEED 7

float IDToColor(uint ID, uint seed)
{
    return float(ID % seed) / float(seed);
}

float IDToColor(uint ID)
{
    return IDToColor(ID, RED_SEED);
}

float2 IDToColor2(uint ID)
{
    float2 color = float2(0, 0);
    color.x = IDToColor(ID, RED_SEED);
    color.y = IDToColor(ID, GREEN_SEED);

    return color;
}

float3 IDToColor3(uint ID)
{
    float3 color = float3(0, 0, 0);
    color.x = IDToColor(ID, RED_SEED);
    color.y = IDToColor(ID, GREEN_SEED);
    color.z = IDToColor(ID, BLUE_SEED);

    return color;
}

float3 CascadeIDToColor(uint cascadeID)
{
    const uint cascadeCount = 8;
    static float3 cascadeColors[cascadeCount] =
    {
        float3(1.0f, 0.0f, 0.0f), // Red
        float3(0.0f, 1.0f, 0.0f), // Green
        float3(0.0f, 0.0f, 1.0f), // Blue
        float3(1.0f, 1.0f, 0.0f), // Yellow
        float3(1.0f, 0.0f, 1.0f), // Purple
        float3(0.0f, 1.0f, 1.0f), // Cyan
        float3(1.0f, 0.5f, 0.0f), // Orange
        float3(0.0f, 0.5f, 1.0f)  // Light Blue
    };

    return cascadeColors[cascadeID % cascadeCount];
}

#endif // VISIBILITYBUFFERS_INCLUDED