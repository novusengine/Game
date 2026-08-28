// CPU counterpart: Animation::AffineFloat3x4. The explicit row vectors avoid
// compiler-dependent matrix-major layout and are the canonical 48-byte palette ABI.
struct AnimationAffineFloat3x4
{
    float4 row0;
    float4 row1;
    float4 row2;
};

float3 AnimationTransformPoint(AnimationAffineFloat3x4 transform, float3 point)
{
    float4 homogeneous = float4(point, 1.0f);
    return float3(dot(transform.row0, homogeneous), dot(transform.row1, homogeneous), dot(transform.row2, homogeneous));
}

float3 AnimationTransformVector(AnimationAffineFloat3x4 transform, float3 vector)
{
    float4 homogeneous = float4(vector, 0.0f);
    return float3(dot(transform.row0, homogeneous), dot(transform.row1, homogeneous), dot(transform.row2, homogeneous));
}
