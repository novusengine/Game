#ifndef VISIBILITYBUFFERS_INCLUDED
#define VISIBILITYBUFFERS_INCLUDED

#include "DescriptorSet/Global.inc.slang"

#include "Model/ModelShared.inc.slang"
#include "Terrain/TerrainShared.inc.slang"

// Choose barycentric reconstruction method method (default = plane). For ray, define RECONSTRUCT_BARY_RAY.
//#define RECONSTRUCT_BARY_RAY

// Calculates the local (barycentric coordinates) position of a ray hitting a triangle (Muller-Trumbore algorithm)
// Parameters: p0,p1,p2 -> World space coordinates of triangle
// o -> Origin of ray in world space (Mainly view camera here)
// d-> Unit vector direction of ray from origin
float3 RayTriangleIntersection(float3 p0, float3 p1, float3 p2, float3 o, float3 d)
{
    float3 v0v1 = p1 - p0;
    float3 v0v2 = p2 - p0;
    float3 pvec = cross(d, v0v2);
    float det = dot(v0v1, pvec);
    float invDet = 1 / det;
    float3 tvec = o - p0;
    float u = dot(tvec, pvec) * invDet;
    float3 qvec = cross(tvec, v0v1);
    float v = dot(d, qvec) * invDet;
    float w = 1.0f - v - u;
    return float3(w, u, v);
}

struct BarycentricDeriv
{
    float3 m_lambda;
    float3 m_ddx;
    float3 m_ddy;
};

// Calculate ray differentials for a point in world-space
// Parameters
// - pt0,pt1,pt2 -> world space coordinates of the triangle currently visible on the pixel
// - screenPos -> 2D NDC position of current pixel, range [-1, 1]
// - rayOrigin -> camera position
// 
// This approach produces the same quality as CalcFullBary, but a bit slower
//   with more register allocation and generates more MUL/FMA instructions for matrix multiplication
BarycentricDeriv CalcRayBary(float3 pt0, float3 pt1, float3 pt2, float2 pixelNdc, float3 rayOrigin, float4x4 viewInv, float4x4 projInv, float2 twoOverScreenSize)
{
    BarycentricDeriv ret;

    // 1px offsets in NDC (Y-down screen to NDC Y-up already handled by caller)
    float3 ndcPos = float3(pixelNdc, 1.0);
    float3 ndcDx = float3(pixelNdc + float2(twoOverScreenSize.x, 0), 1.0);
    float3 ndcDy = float3(pixelNdc - float2(0, twoOverScreenSize.y), 1.0);

    // Clip to View and divide by w
    float4 vp = mul(float4(ndcPos, 1.0), projInv);
    float4 vpx = mul(float4(ndcDx, 1.0), projInv);
    float4 vpy = mul(float4(ndcDy, 1.0), projInv);
    vp.xyz /= vp.w;  vpx.xyz /= vpx.w;  vpy.xyz /= vpy.w;

    // Build directions in VIEW (from camera origin), transform to WORLD, normalize once
    float3 rayDir = mul(float4(vp.xyz, 0.0), viewInv).xyz;
    float3 rayDirDx = mul(float4(vpx.xyz, 0.0), viewInv).xyz;
    float3 rayDirDy = mul(float4(vpy.xyz, 0.0), viewInv).xyz;
    rayDir = normalize(rayDir);
    rayDirDx = normalize(rayDirDx);
    rayDirDy = normalize(rayDirDy);

    // Intersection returns (w,u,v); our pipeline stores (b0,b1) = (w,u)
    float3 l = RayTriangleIntersection(pt0, pt1, pt2, rayOrigin, rayDir);
    float3 ldx = RayTriangleIntersection(pt0, pt1, pt2, rayOrigin, rayDirDx);
    float3 ldy = RayTriangleIntersection(pt0, pt1, pt2, rayOrigin, rayDirDy);

    ret.m_lambda = l;                 // (b0,b1,b2) = (w,u,v)
    ret.m_ddx = ldx - l;           // (db0,db1,db2)
    ret.m_ddy = ldy - l;
    return ret;
}

// Computes the partial derivatives of a triangle from the homogeneous clip space vertices
BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 two_over_windowsize)
{
    BarycentricDeriv ret;
    float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));
    //Project points on screen to calculate post projection positions in 2D
    float2 ndc0 = pt0.xy * invW.x;
    float2 ndc1 = pt1.xy * invW.y;
    float2 ndc2 = pt2.xy * invW.z;

    // Computing partial derivatives and prospective correct attribute interpolation with barycentric coordinates
    // Equation for calculation taken from Appendix A of DAIS paper:
    // https://cg.ivd.kit.edu/publications/2015/dais/DAIS.pdf

    // Calculating inverse of determinant(rcp of area of triangle).
    float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));

    //determining the partial derivatives
    // ddx[i] = (y[i+1] - y[i-1])/Determinant
    ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    // sum of partial derivatives.
    float ddxSum = dot(ret.m_ddx, float3(1, 1, 1));
    float ddySum = dot(ret.m_ddy, float3(1, 1, 1));

    // Delta vector from pixel's screen position to vertex 0 of the triangle.
    float2 deltaVec = pixelNdc - ndc0;

    // Calculating interpolated W at point.
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = rcp(interpInvW);
    // The barycentric co-ordinate (m_lambda) is determined by perspective-correct interpolation. 
    // Equation taken from DAIS paper.
    ret.m_lambda.x = interpW * (invW[0] + deltaVec.x * ret.m_ddx.x + deltaVec.y * ret.m_ddy.x);
    ret.m_lambda.y = interpW * (0.0f + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y);
    ret.m_lambda.z = interpW * (0.0f + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z);

    //Scaling from NDC to pixel units
    ret.m_ddx *= two_over_windowsize.x;
    ret.m_ddy *= two_over_windowsize.y;
    ddxSum *= two_over_windowsize.x;
    ddySum *= two_over_windowsize.y;

    ret.m_ddy *= -1.0f;
    ddySum *= -1.0f;

    // This part fixes the derivatives error happening for the projected triangles.
    // Instead of calculating the derivatives constantly across the 2D triangle we use a projected version
    // of the gradients, this is more accurate and closely matches GPU raster behavior.
    // Final gradient equation: ddx = (((lambda/w) + ddx) / (w+|ddx|)) - lambda

    // Calculating interpW at partial derivatives position sum.
    float interpW_ddx = 1.0f / (interpInvW + ddxSum);
    float interpW_ddy = 1.0f / (interpInvW + ddySum);

    // Calculating perspective projected derivatives.
    ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
    ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

    return ret;
}

#if GEOMETRY_PASS
uint2 PackVisibilityBuffer(uint typeID, uint instanceID, uint triangleID)
{
    // VisibilityBuffer is 4 uints packed like this:
    // X
    //      TriangleID 8 bits (out of 16), we had to split this to fit
    //      InstanceID 20 bits, different types can have the same InstanceID
    //      TypeID 4 bits
    // Y
    //      TriangleID 8 bits (out of 16), the remainder
    //      UNUSED 24 bits

    uint2 packedVisBuffer;
    // X
    packedVisBuffer.x = (triangleID & 0xFF);
    packedVisBuffer.x |= instanceID << 8;
    packedVisBuffer.x |= typeID << 28;
    // Y
    packedVisBuffer.y = (triangleID >> 8);

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

[[vk::binding(2, PER_PASS)]] Texture2D<uint2> _visibilityBuffer;

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
};

uint2 LoadVisibilityBuffer(uint2 pixelPos)
{
    return _visibilityBuffer[pixelPos];
}

const VisibilityBuffer UnpackVisibilityBuffer(uint2 data)
{
    // VisibilityBuffer is 2 uints packed like this:
    // X
    //      TriangleID 8 bits (out of 16), we had to split this to fit
    //      InstanceID 20 bits, different types can have the same InstanceID
    //      TypeID 4 bits
    // Y
    //      TriangleID 8 bits (out of 16), the remainder
    //      UNUSED 24 bits

    VisibilityBuffer vBuffer;
    vBuffer.typeID = data.x >> 28;
    vBuffer.instanceID = (data.x & 0x0FFFFF00) >> 8;

    vBuffer.triangleID = (data.x & 0xFF) | ((data.y & 0xFF) << 8);

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

// Shared helper for reconstructing barycentrics (plane vs ray path).
Barycentrics ReconstructBarycentrics(float2 pixelPos, float2 renderSize, float3 wP0, float3 wP1, float3 wP2, uint cameraIndex)
{
    float2 pixelCenter = pixelPos + 0.5f;
    float2 pixelNdc = pixelCenter / renderSize * 2.0f - 1.0f;
    pixelNdc.y *= -1.0f; // flip once if screen Y-down
    float2 twoOverScreenSize = 2.0f / renderSize;

    Camera cam = _cameras[cameraIndex];
    BarycentricDeriv bd;

#if defined(RECONSTRUCT_BARY_RAY)
    // Ray method expects inverse-projection to view space and inverse-view to world.
    float4x4 projInv = cam.clipToView;
    float4x4 viewInv = cam.viewToWorld;
    float3   camWS = cam.eyePosition.xyz;

    bd = CalcRayBary(wP0, wP1, wP2, pixelNdc, camWS, viewInv, projInv, twoOverScreenSize);
#else
    // Plane/projection path
    float4x4 worldToClip = cam.worldToClip;
    float4 c0 = mul(float4(wP0, 1.0f), worldToClip);
    float4 c1 = mul(float4(wP1, 1.0f), worldToClip);
    float4 c2 = mul(float4(wP2, 1.0f), worldToClip);
    bd = CalcFullBary(c0, c1, c2, pixelNdc, twoOverScreenSize);
#endif

    Barycentrics useBary;
    useBary.bary = bd.m_lambda.xy;
    useBary.ddxBary = bd.m_ddx.xy;
    useBary.ddyBary = bd.m_ddy.xy;
    return useBary;
}

struct PixelVertexData
{
    uint typeID;
    uint drawCallID; // Same as instanceID for terrain
    uint extraID; // Only used for models
    FullBary2 uv0;
    FullBary2 uv1; // Only used for models, for terrain it's a copy of uv0
    float3 color; // Only used for terrain, for models it's hardcoded to white
    float3 worldNormal;
    float3 worldPos;
    float4 viewPos;
    float2 pixelPos;
    float highlightIntensity;
};

PixelVertexData GetPixelVertexDataTerrain(const uint2 pixelPos, const VisibilityBuffer vBuffer, const uint cameraIndex, float2 renderSize)
{
    InstanceData cellInstance = _instanceDatas[vBuffer.instanceID];
    uint globalCellID = cellInstance.globalCellID;
    uint3 localVertexIDs = GetLocalTerrainVertexIDs(vBuffer.triangleID);

    const uint cellID = cellInstance.packedChunkCellID & 0xFFFF;
    const uint chunkID = cellInstance.packedChunkCellID >> 16;

    uint globalVertexOffset = globalCellID * NUM_VERTICES_PER_CELL;

    TerrainVertex vertices[3];
    [unroll]
    for (uint i = 0; i < 3; i++) 
    {
        vertices[i] = LoadTerrainVertex(chunkID, cellID, globalVertexOffset, localVertexIDs[i]);
    }

    float3 wP0 = vertices[0].position;
    float3 wP1 = vertices[1].position;
    float3 wP2 = vertices[2].position;

    Barycentrics bary = ReconstructBarycentrics(pixelPos, renderSize, wP0, wP1, wP2, cameraIndex);

    PixelVertexData result;
    result.typeID = vBuffer.typeID;
    result.drawCallID = vBuffer.instanceID;
    result.extraID = 0;

    result.uv0 = CalcFullBary2(bary, vertices[0].uv, vertices[1].uv, vertices[2].uv);
    result.uv1 = result.uv0;
    result.color = InterpolateVertexAttribute(bary, vertices[0].color, vertices[1].color, vertices[2].color);

    result.worldNormal = normalize(InterpolateVertexAttribute(bary, vertices[0].normal, vertices[1].normal, vertices[2].normal));
    result.worldPos = InterpolateVertexAttribute(bary, vertices[0].position, vertices[1].position, vertices[2].position);
    result.viewPos = mul(float4(result.worldPos, 1.0f), _cameras[cameraIndex].worldToView);
    result.pixelPos = pixelPos;

    result.highlightIntensity = 1.0f; // No highlight for terrain... yet?

    return result;
}

PixelVertexData GetPixelVertexDataModel(const uint2 pixelPos, const VisibilityBuffer vBuffer, const uint cameraIndex, float2 renderSize)
{
    InstanceRef instanceRef = GetOpaqueModelInstanceID(vBuffer.instanceID);
    uint instanceID = instanceRef.instanceID;
    uint drawID = instanceRef.drawID;

    ModelInstanceData instanceData = _modelInstanceDatas[instanceID];
    IndexedDraw draw = _modelDraws[drawID];
    uint3 vertexIDs = GetVertexIDs(vBuffer.triangleID, draw, _modelIndices);

    ModelVertex vertices[3];
    [unroll]
    for (uint i = 0; i < 3; i++) 
    {
        vertices[i] = LoadModelVertex(vertexIDs[i]);
        if (instanceData.boneMatrixOffset != 4294967295)
        {
            float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertices[i]);
            vertices[i].position = mul(float4(vertices[i].position, 1.0f), boneTransformMatrix).xyz;
            vertices[i].normal = mul(vertices[i].normal, (float3x3)boneTransformMatrix);
        }
    }

    float4x4 instanceMatrix = _modelInstanceMatrices[instanceID];
    float3 wP0 = mul(float4(vertices[0].position, 1.0f), instanceMatrix).xyz;
    float3 wP1 = mul(float4(vertices[1].position, 1.0f), instanceMatrix).xyz;
    float3 wP2 = mul(float4(vertices[2].position, 1.0f), instanceMatrix).xyz;

    Barycentrics bary = ReconstructBarycentrics(pixelPos, renderSize, wP0, wP1, wP2, cameraIndex);

    PixelVertexData result;
    result.typeID = vBuffer.typeID;
    result.drawCallID = instanceRef.drawID;
    result.extraID = instanceRef.extraID;

    result.uv0 = CalcFullBary2(bary, vertices[0].uv01.xy, vertices[1].uv01.xy, vertices[2].uv01.xy);
    result.uv1 = CalcFullBary2(bary, vertices[0].uv01.zw, vertices[1].uv01.zw, vertices[2].uv01.zw);
    result.color = float3(1, 1, 1);

    //result.uv0.value = (vertices[0].uv01.xy + vertices[1].uv01.xy + vertices[2].uv01.xy) / 3.0f;

    float3 vertexNormal = normalize(InterpolateVertexAttribute(bary, vertices[0].normal, vertices[1].normal, vertices[2].normal));
    result.worldNormal = normalize(mul(vertexNormal, (float3x3)instanceMatrix));

    float3 pixelVertexPos = InterpolateVertexAttribute(bary, vertices[0].position, vertices[1].position, vertices[2].position);
    result.worldPos = mul(float4(pixelVertexPos, 1.0f), instanceMatrix).xyz;
    result.viewPos = mul(float4(result.worldPos, 1.0f), _cameras[cameraIndex].worldToView);
    result.pixelPos = pixelPos;

    result.highlightIntensity = instanceData.highlightIntensity;

    return result;
}

PixelVertexData GetPixelVertexData(const uint2 pixelPos, const VisibilityBuffer vBuffer, uint cameraIndex, float2 renderSize)
{
    if (vBuffer.typeID == ObjectType::Terrain)
    {
        return GetPixelVertexDataTerrain(pixelPos, vBuffer, cameraIndex, renderSize);
    }
    else if (vBuffer.typeID == ObjectType::ModelOpaque)
    {
        return GetPixelVertexDataModel(pixelPos, vBuffer, cameraIndex, renderSize);
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