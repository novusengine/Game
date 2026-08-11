// Based on the work of WickedEngine, thank you! https://github.com/turanszkij/WickedEngine/
// https://github.com/turanszkij/WickedEngine/blob/master/WickedEngine/shaders/lightCullingCS.hlsl

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Culling.inc.slang"
#include "Include/Debug.inc.slang"
#include "Light/LightShared.inc.slang"

//#define DEBUG_TILEDLIGHTCULLING
#define ADVANCED_CULLING

struct Constants
{
    uint maxDecalsPerTile;
    uint numTotalDecals;
    uint2 tileCount;
    float2 screenSize; // In pixels
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<PackedDecal> _packedDecals; // All decals in the world
[[vk::binding(1, PER_PASS)]] Texture2D<float> _depthRT;

[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _entityTiles;

// Group shared variables.
groupshared uint uMinDepth;
groupshared uint uMaxDepth;
groupshared uint uDepthMask; // Harada Siggraph 2012 2.5D culling
static const uint MAX_DECALS_PER_TILE = 8u;
groupshared uint tileDecalCount;
groupshared uint tileDecalIndices[MAX_DECALS_PER_TILE];
groupshared int tileDecalPriorities[MAX_DECALS_PER_TILE];
groupshared uint tileDecalStableIDs[MAX_DECALS_PER_TILE];
#ifdef DEBUG_TILEDLIGHTCULLING
groupshared uint entityCountDebug;
[[vk::binding(3, PER_PASS)]] RWTexture2D<unorm float4> _debugTexture;
#endif // DEBUG_TILEDLIGHTCULLING

bool DecalKeyLess(int priorityA, uint stableIDA, int priorityB, uint stableIDB)
{
    return priorityA < priorityB || (priorityA == priorityB && stableIDA < stableIDB);
}

void InsertDecal(uint decalIndex, int priority, uint stableID)
{
    uint count = tileDecalCount;
    if (count == MAX_DECALS_PER_TILE)
    {
        InterlockedAdd(_entityTiles[0], 1u);
        if (!DecalKeyLess(tileDecalPriorities[0], tileDecalStableIDs[0], priority, stableID))
            return;
        for (uint index = 1u; index < count; ++index)
        {
            tileDecalIndices[index - 1u] = tileDecalIndices[index];
            tileDecalPriorities[index - 1u] = tileDecalPriorities[index];
            tileDecalStableIDs[index - 1u] = tileDecalStableIDs[index];
        }
        count -= 1u;
    }

    uint insertion = count;
    while (insertion > 0u && DecalKeyLess(priority, stableID, tileDecalPriorities[insertion - 1u], tileDecalStableIDs[insertion - 1u]))
    {
        tileDecalIndices[insertion] = tileDecalIndices[insertion - 1u];
        tileDecalPriorities[insertion] = tileDecalPriorities[insertion - 1u];
        tileDecalStableIDs[insertion] = tileDecalStableIDs[insertion - 1u];
        insertion -= 1u;
    }
    tileDecalIndices[insertion] = decalIndex;
    tileDecalPriorities[insertion] = priority;
    tileDecalStableIDs[insertion] = stableID;
    tileDecalCount = count + 1u;
}

inline uint ConstructEntityMask(in float depthRangeMin, in float depthRangeRecip, in Sphere bounds)
{
	// We create a entity mask to decide if the entity is really touching something
	// If we do an OR operation with the depth slices mask, we instantly get if the entity is contributing or not
	// we do this in view space

	const float fMin = bounds.c.z - bounds.r;
	const float fMax = bounds.c.z + bounds.r;
	const uint __entitymaskcellindexSTART = clamp(floor((fMin - depthRangeMin) * depthRangeRecip), 0, 31);
	const uint __entitymaskcellindexEND = clamp(floor((fMax - depthRangeMin) * depthRangeRecip), 0, 31);

	//// Unoptimized mask construction with loop:
	//// Construct mask from START to END:
	////          END         START
	////	0000000000111111111110000000000
	//uint uLightMask = 0;
	//for (uint c = __entitymaskcellindexSTART; c <= __entitymaskcellindexEND; ++c)
	//{
	//	uLightMask |= 1u << c;
	//}

	// Optimized mask construction, without loop:
	//	- First, fill full mask:
	//	1111111111111111111111111111111
	uint uLightMask = 0xFFFFFFFF;
	//	- Then Shift right with spare amount to keep mask only:
	//	0000000000000000000011111111111
	uLightMask >>= 31u - (__entitymaskcellindexEND - __entitymaskcellindexSTART);
	//	- Last, shift left with START amount to correct mask position:
	//	0000000000111111111110000000000
	uLightMask <<= __entitymaskcellindexSTART;

	return uLightMask;
}

inline AABB WorldAABB_To_DecalSpace(AABB worldBox, float3 decalCenter, float4 decalQuat) {
	float3 r, u, f; 
	BasisFromQuat(decalQuat, r, u, f); // 1) center: translate to decal origin, then project onto basis 

	float3 cRel = worldBox.c - decalCenter; 
	AABB outB; 
	outB.c = float3(dot(cRel, r), dot(cRel, u), dot(cRel, f)); // 2) extents: e' = |M| * e, where M rows are (r,u,f) 
	
	// i.e., abs(row) * ex + abs(row) * ey + abs(row) * ez 
	float3 row0 = abs(r); 
	float3 row1 = abs(u); 
	float3 row2 = abs(f); // multiply rows by source extents components and sum per row 
	outB.e = float3(dot(row0, worldBox.e), // X' 
					dot(row1, worldBox.e), // Y' 
					dot(row2, worldBox.e)  // Z' 
	); 
	return outB; 
}

[shader("compute")]
[numthreads(TILED_CULLING_THREADSIZE, TILED_CULLING_THREADSIZE, 1)]
void main(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
	uint2 dim;
	_depthRT.GetDimensions(dim.x, dim.y);
	float2 dim_rcp = rcp(dim);

	// First thread zeroes out other LDS data:
	if (groupIndex == 0)
	{
		uMinDepth = 0xffffffff;
		uMaxDepth = 0;
		uDepthMask = 0;
		tileDecalCount = 0;

#ifdef DEBUG_TILEDLIGHTCULLING
		entityCountDebug = 0;
#endif //  DEBUG_TILEDLIGHTCULLING
	}

	// Calculate min & max depth in threadgroup / tile.
	float depth[TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY];
	float depthMinUnrolled = 10000000;
	float depthMaxUnrolled = -10000000;

	[unroll]
	for (uint granularity = 0; granularity < TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY; ++granularity)
	{
		uint2 pixel = DTid.xy * uint2(TILED_CULLING_GRANULARITY, TILED_CULLING_GRANULARITY) + Unflatten2D(granularity, TILED_CULLING_GRANULARITY);
		pixel = min(pixel, dim - 1); // avoid loading from outside the texture, it messes up the min-max depth!
		depth[granularity] = _depthRT[pixel];
		depthMinUnrolled = min(depthMinUnrolled, depth[granularity]);
		depthMaxUnrolled = max(depthMaxUnrolled, depth[granularity]);
	}

	GroupMemoryBarrierWithGroupSync();

	float wave_local_min = WaveActiveMin(depthMinUnrolled);
	float wave_local_max = WaveActiveMax(depthMaxUnrolled);
	if (WaveIsFirstLane())
	{
		InterlockedMin(uMinDepth, asuint(wave_local_min));
		InterlockedMax(uMaxDepth, asuint(wave_local_max));
	}

	GroupMemoryBarrierWithGroupSync();

	// reversed depth buffer!
	float fMinDepth = asfloat(uMaxDepth);
	float fMaxDepth = asfloat(uMinDepth);

	fMaxDepth = max(0.000001, fMaxDepth); // fix for AMD!!!!!!!!!

	Frustum GroupFrustum;
	AABB GroupAABB;			// frustum AABB around min-max depth in View Space
	AABB GroupAABB_WS;		// frustum AABB in world space
	float3 viewSpace[8]; // View space frustum corners
	{
		// Top left point, near
		viewSpace[0] = ScreenToView(float4(Gid.xy * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Top right point, near
		viewSpace[1] = ScreenToView(float4(float2(Gid.x + 1, Gid.y) * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Bottom left point, near
		viewSpace[2] = ScreenToView(float4(float2(Gid.x, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Bottom right point, near
		viewSpace[3] = ScreenToView(float4(float2(Gid.x + 1, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMinDepth, 1.0f), dim_rcp).xyz;
		// Top left point, far
		viewSpace[4] = ScreenToView(float4(Gid.xy * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;
		// Top right point, far
		viewSpace[5] = ScreenToView(float4(float2(Gid.x + 1, Gid.y) * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;
		// Bottom left point, far
		viewSpace[6] = ScreenToView(float4(float2(Gid.x, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;
		// Bottom right point, far
		viewSpace[7] = ScreenToView(float4(float2(Gid.x + 1, Gid.y + 1) * TILED_CULLING_BLOCKSIZE, fMaxDepth, 1.0f), dim_rcp).xyz;

		// Left plane
		GroupFrustum.planes[0] = ComputePlane(viewSpace[2], viewSpace[0], viewSpace[4]);
		// Right plane
		GroupFrustum.planes[1] = ComputePlane(viewSpace[1], viewSpace[3], viewSpace[5]);
		// Top plane
		GroupFrustum.planes[2] = ComputePlane(viewSpace[0], viewSpace[1], viewSpace[4]);
		// Bottom plane
		GroupFrustum.planes[3] = ComputePlane(viewSpace[3], viewSpace[2], viewSpace[6]);

		// I construct an AABB around the minmax depth bounds to perform tighter culling:
		// The frustum is asymmetric so we must consider all corners!
		float3 minAABB = 10000000;
		float3 maxAABB = -10000000;
		[unroll]
		for (uint i = 0; i < 8; ++i)
		{
			minAABB = min(minAABB, viewSpace[i]);
			maxAABB = max(maxAABB, viewSpace[i]);
		}

		AABBfromMinMax(GroupAABB, minAABB, maxAABB);

		// We can perform coarse AABB intersection tests with this:
		GroupAABB_WS = GroupAABB;
		AABBtransform(GroupAABB_WS, _cameras[0].viewToWorld);//AABBtransform(GroupAABB_WS, GetCamera().inverse_view);
	}

	// Convert depth values to view space.
	float minDepthVS = ScreenToView(float4(0, 0, fMinDepth, 1), dim_rcp).z;
	float maxDepthVS = ScreenToView(float4(0, 0, fMaxDepth, 1), dim_rcp).z;
	float nearClipVS = ScreenToView(float4(0, 0, 1, 1), dim_rcp).z;

#ifdef ADVANCED_CULLING
	// We divide the minmax depth bounds to 32 equal slices
	// then we mark the occupied depth slices with atomic or from each thread
	// we do all this in linear (view) space
	const float __depthRangeRecip = 31.0f / (maxDepthVS - minDepthVS);
	uint __depthmaskUnrolled = 0;

	[unroll]
		for (uint granularity = 0; granularity < TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY; ++granularity)
		{
			float realDepthVS = ScreenToView(float4(0, 0, depth[granularity], 1), dim_rcp).z;
			const uint __depthmaskcellindex = max(0, min(31, floor((realDepthVS - minDepthVS) * __depthRangeRecip)));
			__depthmaskUnrolled |= 1u << __depthmaskcellindex;
		}

	uint wave_depth_mask = WaveActiveBitOr(__depthmaskUnrolled);
	if (WaveIsFirstLane())
	{
		InterlockedOr(uDepthMask, wave_depth_mask);
	}
#endif

	GroupMemoryBarrierWithGroupSync();

	const uint depth_mask = uDepthMask; // take out from groupshared into register

    if (groupIndex == 0)
    {
		for (uint i = 0; i < _constants.numTotalDecals; ++i)
		{
			PackedDecal packedDecal = _packedDecals[i];
			Decal decal = UnpackDecal(packedDecal);

			float3 positionVS = mul(float4(decal.position, 1), _cameras[0].worldToView).xyz;
			const float radius = length(decal.extents.xyz);
			Sphere sphere = { positionVS.xyz, radius };
			if (SphereInsideFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
			{
				// unit AABB:
				AABB a;
				a.c = 0;
				a.e = decal.extents.xyz;

				// frustum AABB in world space transformed into the space of the probe/decal OBB
				AABB b = GroupAABB_WS;
				b = WorldAABB_To_DecalSpace(b, decal.position, decal.rotationQuat);

				// TODO: More accurate culling? We see the decal getting classified into tiles that are outside the AABB,
				// probably because we're doing this check in decal space and not viewspace, but this is good enough for now

				if (IntersectAABB(a, b))
				{
#ifdef ADVANCED_CULLING
					if (depth_mask & ConstructEntityMask(minDepthVS, __depthRangeRecip, sphere))
#endif
					{
						InsertDecal(i, decal.priority, decal.stableID);
					}
				}
			}
		}

#ifdef DEBUG_TILEDLIGHTCULLING
		entityCountDebug = tileDecalCount;
#endif
	}

	GroupMemoryBarrierWithGroupSync();

	const uint flatTileIndex = Flatten2D(Gid.xy, _constants.tileCount.xy);
	const uint tileAddress = 1u + flatTileIndex * (1u + MAX_DECALS_PER_TILE);
	if (groupIndex == 0)
		_entityTiles[tileAddress] = tileDecalCount;
	for (uint index = groupIndex; index < MAX_DECALS_PER_TILE; index += TILED_CULLING_THREADSIZE * TILED_CULLING_THREADSIZE)
		_entityTiles[tileAddress + 1u + index] = index < tileDecalCount ? tileDecalIndices[index] : 0u;

#ifdef DEBUG_TILEDLIGHTCULLING
	for (uint granularity = 0; granularity < TILED_CULLING_GRANULARITY * TILED_CULLING_GRANULARITY; ++granularity)
	{
		uint2 pixel = DTid.xy * uint2(TILED_CULLING_GRANULARITY, TILED_CULLING_GRANULARITY) + Unflatten2D(granularity, TILED_CULLING_GRANULARITY);

		const float3 mapTex[] = {
			float3(0,0,0),
			float3(0,0,1),
			float3(0,1,1),
			float3(0,1,0),
			float3(1,1,0),
			float3(1,0,0),
		};
		const uint mapTexLen = 5;
		const uint maxHeat = _constants.maxDecalsPerTile;
		float l = saturate((float)entityCountDebug / maxHeat) * mapTexLen;
		float3 a = mapTex[floor(l)];
		float3 b = mapTex[ceil(l)];
		float4 heatmap = float4(lerp(a, b, l - floor(l)), 0.8);
		_debugTexture[pixel] = heatmap;
	}
#endif // DEBUG_TILEDLIGHTCULLING
}
