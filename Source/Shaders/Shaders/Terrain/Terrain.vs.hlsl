/*#include "globalData.inc.hlsl"
#include "Terrain/TerrainShared.inc.hlsl"

struct PackedTerrainVertex
{
    uint packed0;
    uint packed1;
}; // 8 bytes

struct TerrainVertex
{
    float3 position;

    float3 normal;
    float3 color;
    float2 uv;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<PackedTerrainVertex> _packedTerrainVertices;

float3 UnpackTerrainNormal(uint encoded)
{
    uint x = encoded & 0x000000FFu;
    uint y = (encoded >> 8u) & 0x000000FFu;
    uint z = (encoded >> 16u) & 0x000000FFu;

    float3 normal = (float3(x, y, z) / 127.0f) - 1.0f;
    return normalize(normal);
}

float3 UnpackColor(uint encoded)
{
    float r = (float)((encoded) & 0xFF);
    float g = (float)((encoded >> 8) & 0xFF);
    float b = (float)((encoded >> 16) & 0xFF);

    return float3(r, g, b) / 127.0f;
}

float UnpackHalf(uint encoded)
{
    return f16tof32(encoded);
}

TerrainVertex UnpackTerrainVertex(const PackedTerrainVertex packedVertex)
{
    // The vertex consists of 8 bytes of data, we split this into two uints called data0 and data1
    // data0 contains, in order:
    // u8 normal.x
    // u8 normal.y
    // u8 normal.z
    // u8 color.r
    //
    // data1 contains, in order:
    // u8 color.g
    // u8 color.b
    // half height, 2 bytes

    TerrainVertex vertex;

    // Unpack normal and color
    uint normal = packedVertex.packed0;// & 0x00FFFFFFu;
    uint color = ((packedVertex.packed1 & 0x0000FFFFu) << 8u) | (packedVertex.packed0 >> 24u);

    vertex.normal = UnpackTerrainNormal(normal);
    vertex.color = UnpackColor(color);

    // Unpack height
    uint height = packedVertex.packed1 >> 16u;
    vertex.position.y = UnpackHalf(height);

    return vertex;
}

float2 GetGlobalVertexPosition(uint chunkID, uint cellID, uint vertexID)
{
    const int chunkY = (chunkID % NUM_CHUNKS_PER_MAP_SIDE) * NUM_CELLS_PER_CHUNK_SIDE;
    const int chunkX = (chunkID / NUM_CHUNKS_PER_MAP_SIDE) * NUM_CELLS_PER_CHUNK_SIDE;

    const int cellX = ((cellID % NUM_CELLS_PER_CHUNK_SIDE) + chunkX);
    const int cellY = ((cellID / NUM_CELLS_PER_CHUNK_SIDE) + chunkY);

    const int vX = vertexID % 17;
    const int vY = vertexID / 17;

    bool isOddRow = vX > 8.01;

    float2 vertexOffset;
    vertexOffset.x = -(8.5 * isOddRow);
    vertexOffset.y = (0.5 * isOddRow);

    int2 globalVertex = int2(vX + cellX * 8, vY + cellY * 8);

    float2 finalPos = -MAP_HALF_SIZE + (float2(globalVertex)+vertexOffset) * PATCH_SIDE_SIZE;

    return float2(-finalPos.y, -finalPos.x);
}

float2 GetCellSpaceVertexPosition(uint vertexID)
{
    float vertexX = vertexID % 17.0f;
    float vertexY = floor(vertexID / 17.0f);

    bool isOddRow = vertexX > 8.01f;
    vertexX = vertexX - (8.5f * isOddRow);
    vertexY = vertexY + (0.5f * isOddRow);

    // We go from a 2D coordinate system where x is Positive East & y is Positive South
    // we translate this into 3D where x is Positive North & y is Positive West
    return float2(-vertexY, -vertexX);
}

TerrainVertex LoadTerrainVertex(uint chunkID, uint cellID, uint vertexBaseOffset, uint vertexID)
{
    // Load height
    const uint vertexIndex = vertexBaseOffset + vertexID;
    const PackedTerrainVertex packedVertex = _packedTerrainVertices[vertexIndex];
    float2 vertexPos = GetCellSpaceVertexPosition(vertexID);

    TerrainVertex vertex = UnpackTerrainVertex(packedVertex);

    vertex.position.xz = GetGlobalVertexPosition(chunkID, cellID, vertexID);
    vertex.uv = float2(-vertexPos.y, -vertexPos.x); // Negated to go from 3D coordinates to 2D

    return vertex;
}

struct InstanceData
{
    uint chunkGridIndex;
};

[[vk::binding(1, PER_PASS)]] StructuredBuffer<InstanceData> _instanceDatas;

struct VSInput
{
	uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
	float4 position : SV_POSITION;
    float3 color : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    InstanceData instanceData = _instanceDatas[input.instanceID];

    uint cellID = floor(input.vertexID / 145.0f);
    uint cellVertexID = input.vertexID % 145;

    uint vertexBaseOffset = (cellID * NUM_VERTICES_PER_CELL) + (input.instanceID * NUM_CELLS_PER_CHUNK * NUM_VERTICES_PER_CELL);
    TerrainVertex vertex = LoadTerrainVertex(instanceData.chunkGridIndex, cellID, vertexBaseOffset, cellVertexID);

    VSOutput output;
    output.position = mul(float4(vertex.position, 1.0f), _cameras[0].worldToClip);
    output.color = vertex.color;

    return output;
}*/

void main()
{

}