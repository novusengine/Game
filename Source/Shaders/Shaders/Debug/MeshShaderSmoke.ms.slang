struct MeshVertex
{
    float4 position : SV_Position;
    float3 color : Color;
};

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void main(out vertices MeshVertex vertices[3], out indices uint3 triangles[1])
{
    SetMeshOutputCounts(3, 1);

    vertices[0].position = float4(-0.65, -0.55, 0.0, 1.0);
    vertices[0].color = float3(1.0, 0.1, 0.8);
    vertices[1].position = float4(0.0, 0.65, 0.0, 1.0);
    vertices[1].color = float3(0.1, 1.0, 0.9);
    vertices[2].position = float4(0.65, -0.55, 0.0, 1.0);
    vertices[2].color = float3(1.0, 0.85, 0.1);
    triangles[0] = uint3(0, 1, 2);
}
