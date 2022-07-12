#include "Terrain/terrain.inc.hlsl"

struct Constants
{
	float target;

	uint width;
	uint height;
	uint depth;
	uint border;
};

struct Vertex
{
	float4 position;
	float4 normal;
};

// Inputs
[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_DRAW)]] StructuredBuffer<int> _triangleConnectionTable;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<int> _cubeEdgeFlags;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<float> _voxels;

[[vk::binding(4, PER_DRAW)]] RWByteAddressBuffer _arguments; // VkDrawIndirectCommand
[[vk::binding(5, PER_DRAW)]] RWStructuredBuffer<Vertex> _vertices;

// edgeConnection lists the index of the endpoint vertices for each of the 12 edges of the cube
static int2 edgeConnection[12] =
{
	int2(0,1), int2(1,2), int2(2,3), int2(3,0), int2(4,5), int2(5,6), int2(6,7), int2(7,4), int2(0,4), int2(1,5), int2(2,6), int2(3,7)
};

// edgeDirection lists the direction vector (vertex1-vertex0) for each edge in the cube
static float3 edgeDirection[12] =
{
	float3(1.0f, 0.0f, 0.0f),float3(0.0f, 1.0f, 0.0f),float3(-1.0f, 0.0f, 0.0f),float3(0.0f, -1.0f, 0.0f),
	float3(1.0f, 0.0f, 0.0f),float3(0.0f, 1.0f, 0.0f),float3(-1.0f, 0.0f, 0.0f),float3(0.0f, -1.0f, 0.0f),
	float3(0.0f, 0.0f, 1.0f),float3(0.0f, 0.0f, 1.0f),float3(0.0f, 0.0f, 1.0f),float3(0.0f,  0.0f, 1.0f)
};

// vertexOffset lists the positions, relative to vertex0, of each of the 8 vertices of a cube
static float3 vertexOffset[8] =
{
	float3(0, 0, 0),float3(1, 0, 0),float3(1, 1, 0),float3(0, 1, 0),
	float3(0, 0, 1),float3(1, 0, 1),float3(1, 1, 1),float3(0, 1, 1)
};

float3 GetNormal(int x, int y, int z)
{
	const int width = _constants.width;
	const int height = _constants.height;

	int id = x + (y * width) + (z * width * height);
	float value = _voxels[id];

	float3 normal;

	int xNegID = (x - 1) + (y * width) + (z * width * height);
	int xPosID = (x + 1) + (y * width) + (z * width * height);
	normal.x = _voxels[xNegID] - _voxels[xPosID];

	int yNegID = x + ((y - 1) * width) + (z * width * height);
	int yPosID = x + ((y + 1) * width) + (z * width * height);
	normal.y = _voxels[yNegID] - _voxels[yPosID];

	int zNegID = x + (y * width) + ((z - 1) * width * height);
	int zPosID = x + (y * width) + ((z + 1) * width * height);
	normal.z = _voxels[zNegID] - _voxels[zPosID];

	return normal;
}

void FillCube(int x, int y, int z, out float cube[8])
{
	const int width = _constants.width;
	const int height = _constants.height;

	cube[0] = _voxels[x + y * width + z * width * height];
	cube[1] = _voxels[(x + 1) + y * width + z * width * height];
	cube[2] = _voxels[(x + 1) + (y + 1) * width + z * width * height];
	cube[3] = _voxels[x + (y + 1) * width + z * width * height];

	cube[4] = _voxels[x + y * width + (z + 1) * width * height];
	cube[5] = _voxels[(x + 1) + y * width + (z + 1) * width * height];
	cube[6] = _voxels[(x + 1) + (y + 1) * width + (z + 1) * width * height];
	cube[7] = _voxels[x + (y + 1) * width + (z + 1) * width * height];
}

// GetOffset finds the approximate point of intersection of the surface
// between two points with the values v1 and v2
float GetOffset(float v1, float v2)
{
	float delta = v2 - v1;
	return (delta == 0.0f) ? 0.5f : (_constants.target - v1) / delta;
}

Vertex CreateVertex(float3 position, float3 center, float3 size, float3 normal)
{
	Vertex vertex;
	vertex.position = float4(position - center, 1.0);
	vertex.normal = float4(normal, 1.0);

	return vertex;
}

struct CSInput
{
	int3 dispatchThreadID : SV_DispatchThreadID;
};

[numthreads(4, 4, 4)]
void main(CSInput input)
{
	int3 id = input.dispatchThreadID;

	const int width = _constants.width;
	const int height = _constants.height;
	const int depth = _constants.depth;
	const int border = _constants.border;

	if (id.x >= width - 1 - border)
		return;
	if (id.y >= height - 1 - border)
		return;
	if (id.z >= depth - 1 - border)
		return;

	float3 pos = float3(id);
	float3 normal = normalize(GetNormal(id.x, id.y, id.z));
	float3 center = float3(width, 0, depth) / 2.0f;

	float cube[8];
	FillCube(id.x, id.y, id.z, cube);

	int flagIndex = 0;
	float3 edgeVertex[12];

	// Find which vertices are inside of the surface and which are outside
	for (int i = 0; i < 8; i++)
	{
		if (cube[i] <= _constants.target)
		{
			flagIndex |= 1u << i;
		}
	}

	//Find which edges are intersected by the surface
	int edgeFlags = _cubeEdgeFlags[flagIndex];

	// no connections, return
	if (edgeFlags == 0)
		return;

	// Find the point of intersection of the surface with each edge
	for (int i = 0; i < 12; i++)
	{
		// If there is an intersection on this edge
		if ((edgeFlags & (1u << i)) != 0)
		{
			float offset = GetOffset(cube[edgeConnection[i].x], cube[edgeConnection[i].y]);

			edgeVertex[i] = pos + (vertexOffset[edgeConnection[i].x] + offset * edgeDirection[i]);
		}
	}

	float3 size = float3(width - 1, height - 1, depth - 1);

	int idx = id.x + (id.y * width) + (id.z * width * height);

	// Save the triangles that were found. There can be up to five per cube
	for (int i = 0; i < 5; i++)
	{
		// If the connection table is not -1 then this is a triangle
		if (_triangleConnectionTable[flagIndex * 16 + 3 * i] >= 0)
		{
			/*uint offset;
			_arguments.InterlockedAdd(0, 3, offset);

			float3 position = edgeVertex[_triangleConnectionTable[flagIndex * 16 + (3 * i + 0)]];
			_vertices[offset + 0] = CreateVertex(position, center, size);

			position = edgeVertex[_triangleConnectionTable[flagIndex * 16 + (3 * i + 1)]];
			_vertices[offset + 1] = CreateVertex(position, center, size);

			position = edgeVertex[_triangleConnectionTable[flagIndex * 16 + (3 * i + 2)]];
			_vertices[offset + 2] = CreateVertex(position, center, size);*/

			float3 position = edgeVertex[_triangleConnectionTable[flagIndex * 16 + (3 * i + 0)]];
			_vertices[idx * 15 + (3 * i + 0)] = CreateVertex(position, center, size, normal);

			position = edgeVertex[_triangleConnectionTable[flagIndex * 16 + (3 * i + 1)]];
			_vertices[idx * 15 + (3 * i + 1)] = CreateVertex(position, center, size, normal);

			position = edgeVertex[_triangleConnectionTable[flagIndex * 16 + (3 * i + 2)]];
			_vertices[idx * 15 + (3 * i + 2)] = CreateVertex(position, center, size, normal);
		}
	}
}