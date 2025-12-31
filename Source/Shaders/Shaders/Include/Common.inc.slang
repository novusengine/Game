#ifndef COMMON_INCLUDED
#define COMMON_INCLUDED

#define SIZEOF_UINT  4
#define SIZEOF_UINT2 8
#define SIZEOF_UINT3 12
#define SIZEOF_UINT4 16
#define SIZEOF_DRAW_INDIRECT_ARGUMENTS 16

float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * (select(v.xy >= 0.0, 1.0, -1.0));
}

float2 OctNormalEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 OctNormalDecode(float2 f)
{
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

float LinearizeDepth(float d, float zNear, float zFar)
{
    float z_n = 2.0 * d - 1.0;
    return 2.0 * zNear * zFar / (zFar + zNear - z_n * (zFar - zNear));
}

float Map(float value, float originalMin, float originalMax, float newMin, float newMax)
{
    return (value - originalMin) / (originalMax - originalMin) * (newMax - newMin) + newMin;
}

float4 ToFloat4(int input, float defaultAlphaVal)
{
    return float4(input, 0, 0, defaultAlphaVal);
}

float4 ToFloat4(int2 input, float defaultAlphaVal)
{
    return float4(input, 0, defaultAlphaVal);
}

float4 ToFloat4(int3 input, float defaultAlphaVal)
{
    return float4(input, defaultAlphaVal);
}

float4 ToFloat4(int4 input, float defaultAlphaVal)
{
    return float4(input);
}

float4 ToFloat4(uint input, float defaultAlphaVal)
{
    return float4(input, 0, 0, defaultAlphaVal);
}

float4 ToFloat4(uint2 input, float defaultAlphaVal)
{
    return float4(input, 0, defaultAlphaVal);
}

float4 ToFloat4(uint3 input, float defaultAlphaVal)
{
    return float4(input, defaultAlphaVal);
}

float4 ToFloat4(uint4 input, float defaultAlphaVal)
{
    return float4(input);
}

float4 ToFloat4(float input, float defaultAlphaVal)
{
    return float4(input, 0, 0, defaultAlphaVal);
}

float4 ToFloat4(float2 input, float defaultAlphaVal)
{
    return float4(input, 0, defaultAlphaVal);
}

float4 ToFloat4(float3 input, float defaultAlphaVal)
{
    return float4(input, defaultAlphaVal);
}

float4 ToFloat4(float4 input, float defaultAlphaVal)
{
    return input;
}

struct Draw
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

struct IndexedDraw
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    uint vertexOffset;
    uint firstInstance;
};

void WriteDrawToByteAdressBuffer(RWByteAddressBuffer byteAddressBuffer, uint drawOffset, IndexedDraw draw)
{
    uint byteOffset = drawOffset * sizeof(IndexedDraw);

    byteAddressBuffer.Store4(byteOffset, uint4(draw.indexCount, draw.instanceCount, draw.firstIndex, draw.vertexOffset));
    byteAddressBuffer.Store(byteOffset + (4 * sizeof(uint)), draw.firstInstance);
}

uint3 GetVertexIDs(uint triangleID, IndexedDraw draw, StructuredBuffer<uint> indexBuffer)
{
    uint localIndexID = triangleID * 3;

    uint globalIndexID = draw.firstIndex + localIndexID; // This always points to the index of the first vertex in the triangle

    uint3 vertexIDs;
    [unroll]
    for (uint i = 0; i < 3; i++)
    {
        // Our index buffer is made up of uint16_t's, but hardware support for accessing them is kinda bad.
        // So instead we're going to access them as uints, and then treat them as packed pairs of uint16_t's
        uint indexToLoad = globalIndexID + i;
        uint indexPairToLoad = (indexToLoad) / 2;

        uint localVertexIDPair = indexBuffer[indexPairToLoad];

        bool isOdd = indexToLoad % 2 == 1;
        vertexIDs[i] = draw.vertexOffset + ((localVertexIDPair >> (16 * isOdd)) & 0xFFFF);
    }

    return vertexIDs;
}

// This assumes the float components are between 0 and 1, and it will be stored as a unorm
uint Float4ToPackedUnorms(float4 f)
{
    uint packed = 0;
    packed |= uint(f.x * 255) << 0;
    packed |= uint(f.y * 255) << 8;
    packed |= uint(f.z * 255) << 16;
    packed |= uint(f.w * 255) << 24;
    return packed;
}

float4 PackedUnormsToFloat4(uint packed)
{
    float4 f = 0;
    f.x = ((packed >> 0) & 0xFF) / 255.0f;
    f.y = ((packed >> 8) & 0xFF) / 255.0f;
    f.z = ((packed >> 16) & 0xFF) / 255.0f;
    f.w = ((packed >> 24) & 0xFF) / 255.0f;
    return f;
}

// Use quad ops (SM 6+), best match to pixel-shader ddx/ddy:
inline void ComputeGradients_Quad(float2 uv, out float2 ddx, out float2 ddy)
{
    // Across X
    float2 uv_x = QuadReadAcrossX(uv);
    ddx = ((WaveGetLaneIndex() & 1) == 0) ? (uv_x - uv) : (uv - uv_x);

    // Across Y
    float2 uv_y = QuadReadAcrossY(uv);
    ddy = ((WaveGetLaneIndex() & 2) == 0) ? (uv_y - uv) : (uv - uv_y);
}

// Build orthonormal basis from quaternion (row-major)
inline void BasisFromQuat(float4 q, out float3 r, out float3 u, out float3 f)
{
    // q = (x,y,z,w), unit length
    q = normalize(q);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;

    r = float3(1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy));
    u = float3(2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx));
    f = float3(2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy));
}

inline uint2 PixelToTile(uint2 pixel, uint2 tileSize)
{
    // Integer division floors automatically: (0..15)->0, (16..31)->1, etc.
    return pixel / tileSize;
}

// 2D array index to flattened 1D array index
inline uint Flatten2D(uint2 coord, uint2 dim)
{
    return coord.x + coord.y * dim.x;
}

// flattened array index to 2D array index
inline uint2 Unflatten2D(uint idx, uint2 dim)
{
    return uint2(idx % dim.x, idx / dim.x);
}

inline float3x3 QuatToMat(float4 q)
{
    q = normalize(q);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    return float3x3(
        1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy),
        2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx),
        2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy)
    );
}

enum ObjectType : uint
{
    Skybox = 0, // We clear to this color so we won't be writing it
    Terrain = 1,
    ModelOpaque = 2,
    JoltDebug = 3
};

#endif // COMMON_INCLUDED