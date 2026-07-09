#ifndef SDSM_INCLUDED
#define SDSM_INCLUDED

// Shared types and helpers for the SDSM cascade fitting chain:
// DepthMinMax.cs -> CascadeFitRange.cs -> CascadeXYReduce.cs -> CascadeFitCameras.cs
//
// Matrix layout rule (load-bearing): shaders consume glm column-major uploads with
// mul(rowVector, M), so a Slang float4x4 row i must hold what glm stores in column i.
// glm composition C = A * B therefore becomes C_s = mul(B_s, A_s) here.

// Push constants shared by all fit dispatches, identical bytes pushed to each pipeline
struct SDSMConstants
{
    float4 lightDirection; // xyz = direction the light travels
    uint numCascades;
    float cascadeSplitLambda;
    float cascadeTextureSize;
    uint stableShadows;
    float shadowMaxDistance;
    float quantizeStep;
    float shrinkDelay;
    float deltaTime;
    uint useDepthBounds;
    float casterMargin;
    uint useXYBounds; // 0 = sphere fit, 1 = reduce runs for diagnostics only, 2 = XY bounds drive the cameras
    float xyMarginTexels;
};

struct SDSMState
{
    float smoothedMin;
    float smoothedMax;
    float usedMin;
    float usedMax;
    float windowLowestMin;  // Widest bounds seen during the current shrink window
    float windowHighestMax;
    float windowTimer;
    float padding;
};

struct SDSMData
{
    SDSMState state;          // Persistent Z-range hysteresis, float offsets [0..7] are read back by the CPU
    float splitDist[8];       // Split as an NDC-corner lerp fraction, consumed by the slice corner math
    float splitDepth[8];      // Split as view distance - nearClip, mirrors _cameras[i].eyePosition.w semantics
    float4 cascadeStable[8];  // Persistent per cascade: x = bucketHalfX, y = bucketHalfY
    float4 cascadeDiag[8];    // Per frame diagnostics: xyz = fitted extents, w = valid
};

// Constant up for the stable path, falls back to Z when the light is near vertical
float3 StableUp(float3 lightDirection)
{
    return abs(lightDirection.y) > 0.99f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
}

// Matches glm::lookAtLH memory layout (GLM_FORCE_LEFT_HANDED)
float4x4 BuildLookAt(float3 eye, float3 center, float3 up)
{
    float3 f = normalize(center - eye);
    float3 s = normalize(cross(up, f));
    float3 u = cross(f, s);

    float4x4 m;
    m[0] = float4(s.x, u.x, f.x, 0.0f);
    m[1] = float4(s.y, u.y, f.y, 0.0f);
    m[2] = float4(s.z, u.z, f.z, 0.0f);
    m[3] = float4(-dot(s, eye), -dot(u, eye), -dot(f, eye), 1.0f);
    return m;
}

// Rotation-only light basis, lightSpace = mul(cameraRelativeWorld, R). The rows mirror
// BuildLookAt(0, lightDirection, StableUp) term for term so CascadeXYReduce coordinates
// and the CascadeFitCameras view matrix agree bit for bit
float3x3 BuildLightRotation(float3 lightDirection)
{
    float3 f = normalize(lightDirection);
    float3 s = normalize(cross(StableUp(lightDirection), f));
    float3 u = cross(f, s);

    float3x3 m;
    m[0] = float3(s.x, u.x, f.x);
    m[1] = float3(s.y, u.y, f.y);
    m[2] = float3(s.z, u.z, f.z);
    return m;
}

// Analytic inverse of a BuildLookAt matrix (rigid transform)
float4x4 InverseLookAt(float4x4 view, float3 eye)
{
    float3 s = float3(view[0].x, view[1].x, view[2].x);
    float3 u = float3(view[0].y, view[1].y, view[2].y);
    float3 f = float3(view[0].z, view[1].z, view[2].z);

    float4x4 m;
    m[0] = float4(s, 0.0f);
    m[1] = float4(u, 0.0f);
    m[2] = float4(f, 0.0f);
    m[3] = float4(eye, 1.0f);
    return m;
}

// Matches glm::orthoLH_ZO memory layout (GLM_FORCE_DEPTH_ZERO_TO_ONE)
float4x4 BuildOrtho(float left, float right, float bottom, float top, float zNear, float zFar)
{
    float4x4 m;
    m[0] = float4(2.0f / (right - left), 0.0f, 0.0f, 0.0f);
    m[1] = float4(0.0f, 2.0f / (top - bottom), 0.0f, 0.0f);
    m[2] = float4(0.0f, 0.0f, 1.0f / (zFar - zNear), 0.0f);
    m[3] = float4(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -zNear / (zFar - zNear), 1.0f);
    return m;
}

// Analytic inverse of a BuildOrtho matrix, uses the post-snap projection values
float4x4 InverseOrtho(float4x4 proj)
{
    float3 scale = float3(proj[0].x, proj[1].y, proj[2].z);
    float3 translation = proj[3].xyz;

    float4x4 m;
    m[0] = float4(1.0f / scale.x, 0.0f, 0.0f, 0.0f);
    m[1] = float4(0.0f, 1.0f / scale.y, 0.0f, 0.0f);
    m[2] = float4(0.0f, 0.0f, 1.0f / scale.z, 0.0f);
    m[3] = float4(-translation / scale, 1.0f);
    return m;
}

// glm row i of M, planes are extracted from mathematical rows
float4 MatrixRow(float4x4 m, uint i)
{
    return float4(m[0][i], m[1][i], m[2][i], m[3][i]);
}

// Gribb-Hartmann row (inside if dot(n, p) + d >= 0) to the culling encoding vec4(n, dot(n, pointOnPlane))
float4 ExtractPlane(float4 row)
{
    float normalLength = length(row.xyz);
    return float4(row.xyz / normalLength, -row.w / normalLength);
}

// Order-preserving float <-> uint for atomic min/max on signed light-space coordinates.
// Branches on the stored sign bit so -0.0 orders below +0.0. Sentinels: 0xFFFFFFFF orders
// above all real keys, 0x00000000 below, so an untouched slot decodes as min > max (invalid)
uint EncodeLightSpaceCoord(float f)
{
    uint u = asuint(f);
    return (u & 0x80000000u) != 0 ? ~u : (u | 0x80000000u);
}

float DecodeLightSpaceCoord(uint u)
{
    return asfloat((u & 0x80000000u) != 0 ? (u ^ 0x80000000u) : ~u);
}

#endif // SDSM_INCLUDED
