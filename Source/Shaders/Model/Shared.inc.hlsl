#ifndef MODEL_SHARED_INCLUDED
#define MODEL_SHARED_INCLUDED

struct ModelData
{
    uint meshletOffset;
    uint packedData; // 16 bits meshletCount, 16 bits textureID
    uint indexOffset;
    uint vertexOffset;
};

struct InstanceData
{
    uint modelDataID;
    uint padding;
};

struct SurvivedInstanceData
{
    uint instanceDataID;
    uint numMeshletsBeforeInstance;
};

struct MeshletData
{
    uint indexStart;
    uint indexCount;
};

struct MeshletInstance
{
    uint meshletDataID;
    uint instanceDataID;
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
#endif // MODEL_SHARED_INCLUDED