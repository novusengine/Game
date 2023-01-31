#ifndef DEBUG_INCLUDED
#define DEBUG_INCLUDED
#include "Include/Culling.inc.hlsl"

static const float PI = 3.14159265f;

// AABBGGRR
enum DebugColor : uint
{
	BLACK = 0xFF000000,
	WHITE = 0xFFFFFFFF,
	GRAY = 0xFF808080,

	RED = 0xFF0000FF,
	GREEN = 0xFF00FF00,
	BLUE = 0xFFFF0000,

	CYAN = 0xFFFFFF00,
	MAGENTA = 0xFFFF00FF,
	YELLOW = 0xFF00FFFF,

	PASTEL_RED = 0xFF6169FF,
	PASTEL_GREEN = 0xFF77DD77,
	PASTEL_BLUE = 0xFFE7C7A7,
	PASTEL_PURPLE = 0xFFE1B1C3,
	PASTEL_ORANGE = 0xFF98C8FA,
	PASTEL_YELLOW = 0xFF96FDFD,
};

// 2D Debugging
struct DebugDrawContext2D
{
	uint numVertices;
	uint offset;
};

struct DebugVertex2D
{
	float2 pos;
	uint color;
};

[[vk::binding(0, DEBUG)]] RWStructuredBuffer<DebugVertex2D> _debugVertices2D;
[[vk::binding(1, DEBUG)]] RWByteAddressBuffer _debugVertices2DCount;

// The debug vertices need to be continuous in memory, so we have to allocate them upfront
DebugDrawContext2D StartDraw2D(uint numVertices)
{
	DebugDrawContext2D context;
	context.numVertices = numVertices;
	_debugVertices2DCount.InterlockedAdd(0, numVertices, context.offset);

	return context;
}

void DrawLine2D(inout DebugDrawContext2D context, float2 from, float2 to, uint color)
{
	if (context.offset + 2 <= context.numVertices)
	{
		_debugVertices2D[context.offset].pos = from;
		_debugVertices2D[context.offset++].color = color;

		_debugVertices2D[context.offset].pos = to;
		_debugVertices2D[context.offset++].color = color;
	}
}

void DrawLine2D(float2 from, float2 to, uint color)
{
	DebugDrawContext2D context = StartDraw2D(2);
	DrawLine2D(context, from, to, color);
}

void DrawTriangle2D(inout DebugDrawContext2D context, float2 v0, float2 v1, float2 v2, uint color)
{
	DrawLine2D(context, v0, v1, color);
	DrawLine2D(context, v1, v2, color);
	DrawLine2D(context, v2, v0, color);
}

void DrawTriangle2D(float2 v0, float2 v1, float2 v2, uint color)
{
	DebugDrawContext2D context = StartDraw2D(6);
	DrawLine2D(context, v0, v1, color);
	DrawLine2D(context, v1, v2, color);
	DrawLine2D(context, v2, v0, color);
}

// 3D Debugging
struct DebugDrawContext3D
{
	uint numVertices;
	uint offset;
};

struct DebugVertex3D
{
	float3 pos;
	uint color;
};

[[vk::binding(2, DEBUG)]] RWStructuredBuffer<DebugVertex3D> _debugVertices3D;
[[vk::binding(3, DEBUG)]] RWByteAddressBuffer _debugVertices3DCount;

// The debug vertices need to be continuous in memory, so we have to allocate them upfront
DebugDrawContext3D StartDraw3D(uint numVertices)
{
	DebugDrawContext3D context;
	context.numVertices = numVertices;

	uint offset;
	_debugVertices3DCount.InterlockedAdd(0, numVertices, offset);
	context.offset = offset;

	return context;
}

void DrawLine3D(inout DebugDrawContext3D context, float3 from, float3 to, uint color)
{
	_debugVertices3D[context.offset].pos = from;
	_debugVertices3D[context.offset++].color = color;

	_debugVertices3D[context.offset].pos = to;
	_debugVertices3D[context.offset++].color = color;
}

void DrawLine3D(float3 from, float3 to, uint color)
{
	DebugDrawContext3D context = StartDraw3D(2);
	DrawLine3D(context, from, to, color);
}

void DrawTriangle3D(inout DebugDrawContext3D context, float3 v0, float3 v1, float3 v2, uint color)
{
	DrawLine3D(context, v0, v1, color);
	DrawLine3D(context, v1, v2, color);
	DrawLine3D(context, v2, v0, color);
}

void DrawTriangle3D(float3 v0, float3 v1, float3 v2, uint color)
{
	DebugDrawContext3D context = StartDraw3D(6);
	DrawLine3D(context, v0, v1, color);
	DrawLine3D(context, v1, v2, color);
	DrawLine3D(context, v2, v0, color);
}

void DrawPlane3D(inout DebugDrawContext3D context, float3 v0, float3 v1, float3 v2, float3 v3, uint color)
{
	DrawLine3D(context, v0, v1, color);
	DrawLine3D(context, v1, v2, color);
	DrawLine3D(context, v2, v3, color);
	DrawLine3D(context, v3, v0, color);
}

void DrawPlane3D(float3 v0, float3 v1, float3 v2, float3 v3, uint color)
{
	DebugDrawContext3D context = StartDraw3D(8);
	DrawLine3D(context, v0, v1, color);
	DrawLine3D(context, v1, v2, color);
	DrawLine3D(context, v2, v3, color);
	DrawLine3D(context, v3, v0, color);
}

void DrawAABB3D(inout DebugDrawContext3D context, AABB aabb, uint color)
{
	// Bottom vertices
	float3 v0 = float3(aabb.min.x, aabb.min.y, aabb.min.z);
	float3 v1 = float3(aabb.min.x, aabb.min.y, aabb.max.z);
	float3 v2 = float3(aabb.max.x, aabb.min.y, aabb.max.z);
	float3 v3 = float3(aabb.max.x, aabb.min.y, aabb.min.z);

	// Top vertices
	float3 v4 = float3(aabb.min.x, aabb.max.y, aabb.min.z);
	float3 v5 = float3(aabb.min.x, aabb.max.y, aabb.max.z);
	float3 v6 = float3(aabb.max.x, aabb.max.y, aabb.max.z);
	float3 v7 = float3(aabb.max.x, aabb.max.y, aabb.min.z);

	// Draw bottom
	DrawLine3D(context, v0, v1, color);
	DrawLine3D(context, v1, v2, color);
	DrawLine3D(context, v2, v3, color);
	DrawLine3D(context, v3, v0, color);

	// Draw top
	DrawLine3D(context, v4, v5, color);
	DrawLine3D(context, v5, v6, color);
	DrawLine3D(context, v6, v7, color);
	DrawLine3D(context, v7, v4, color);

	// Draw edges
	DrawLine3D(context, v0, v4, color);
	DrawLine3D(context, v1, v5, color);
	DrawLine3D(context, v2, v6, color);
	DrawLine3D(context, v3, v7, color);
}

void DrawAABB3D(AABB aabb, uint color)
{
	DebugDrawContext3D context = StartDraw3D(24);
	DrawAABB3D(context, aabb, color);
}

float3 Unproject(float3 p, float4x4 m)
{
	float4 obj = mul(float4(p, 1.0f), m);
	obj /= obj.w;
	return obj.xyz;
}

float4x4 inverse(float4x4 m) 
{
	float n11 = m[0][0], n12 = m[1][0], n13 = m[2][0], n14 = m[3][0];
	float n21 = m[0][1], n22 = m[1][1], n23 = m[2][1], n24 = m[3][1];
	float n31 = m[0][2], n32 = m[1][2], n33 = m[2][2], n34 = m[3][2];
	float n41 = m[0][3], n42 = m[1][3], n43 = m[2][3], n44 = m[3][3];

	float t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
	float t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
	float t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
	float t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;

	float det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
	float idet = 1.0f / det;

	float4x4 ret;

	ret[0][0] = t11 * idet;
	ret[0][1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * idet;
	ret[0][2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * idet;
	ret[0][3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * idet;

	ret[1][0] = t12 * idet;
	ret[1][1] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * idet;
	ret[1][2] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * idet;
	ret[1][3] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * idet;

	ret[2][0] = t13 * idet;
	ret[2][1] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * idet;
	ret[2][2] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * idet;
	ret[2][3] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * idet;

	ret[3][0] = t14 * idet;
	ret[3][1] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * idet;
	ret[3][2] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * idet;
	ret[3][3] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * idet;

	return ret;
}

float3x3 inverse(float3x3 m)
{
	float det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
				m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
				m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

	float detInv = 1 / det;

	float3x3 mInv;
	mInv[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) * detInv;
	mInv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * detInv;
	mInv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * detInv;
	mInv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * detInv;
	mInv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * detInv;
	mInv[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * detInv;
	mInv[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) * detInv;
	mInv[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * detInv;
	mInv[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * detInv;

	return mInv;
}

void DrawFrustum3D(float4x4 viewProjMatrix, uint color)
{
	float4x4 m = inverse(viewProjMatrix);

	float3 near0 = Unproject(float3(-1.0f, -1.0f, 0.0f), m);
	float3 near1 = Unproject(float3(+1.0f, -1.0f, 0.0f), m);
	float3 near2 = Unproject(float3(+1.0f, +1.0f, 0.0f), m);
	float3 near3 = Unproject(float3(-1.0f, +1.0f, 0.0f), m);

	float3 far0 = Unproject(float3(-1.0f, -1.0f, 1.0f), m);
	float3 far1 = Unproject(float3(+1.0f, -1.0f, 1.0f), m);
	float3 far2 = Unproject(float3(+1.0f, +1.0f, 1.0f), m);
	float3 far3 = Unproject(float3(-1.0f, +1.0f, 1.0f), m);

	DebugDrawContext3D context = StartDraw3D(24);

	// Near plane
	DrawLine3D(context, near0, near1, DebugColor::RED);
	DrawLine3D(context, near1, near2, DebugColor::RED);
	DrawLine3D(context, near2, near3, DebugColor::RED);
	DrawLine3D(context, near3, near0, DebugColor::RED);


	// Far plane
	DrawLine3D(context, far0, far1, DebugColor::BLUE);
	DrawLine3D(context, far1, far2, DebugColor::BLUE);
	DrawLine3D(context, far2, far3, DebugColor::BLUE);
	DrawLine3D(context, far3, far0, DebugColor::BLUE);

	// Edges
	DrawLine3D(context, near0, far0, DebugColor::GREEN);
	DrawLine3D(context, near1, far1, DebugColor::GREEN);
	DrawLine3D(context, near2, far2, DebugColor::GREEN);
	DrawLine3D(context, near3, far3, DebugColor::GREEN);
}

void DrawMatrix3D(float4x4 mat, float scale)
{
	float3 origin = float3(mat[3].x, mat[3].y, mat[3].z);

	DrawLine3D(origin, origin + (float3(mat[0].x, mat[0].y, mat[0].z) * scale), 0xFF0000FF);
	DrawLine3D(origin, origin + (float3(mat[1].x, mat[1].y, mat[1].z) * scale), 0xFF00FF00);
	DrawLine3D(origin, origin + (float3(mat[2].x, mat[2].y, mat[2].z) * scale), 0xFFFF0000);
}

#endif // DEBUG_INCLUDED