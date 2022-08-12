#ifndef TERRAIN_INCLUDED
#define TERRAIN_INCLUDED

struct ChunkData
{
    uint meshletOffset;
    uint meshletCount;
    uint indexOffset;
    uint vertexOffset;
};

struct SurvivedChunkData
{
    uint chunkDataID;
    uint numMeshletsBeforeChunk;
};

struct MeshletData
{
    uint indexStart;
    uint indexCount;
};

struct SurvivedMeshletData
{
    uint meshletDataID;
    uint chunkDataID;
};

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
float3 IDToColor3(uint ID)
{
    float3 color = float3(0, 0, 0);
    color.x = IDToColor(ID, RED_SEED);
    color.y = IDToColor(ID, GREEN_SEED);
    color.z = IDToColor(ID, BLUE_SEED);

    return color;
}

#endif // TERRAIN_INCLUDED