permutation DEBUG_ID = [0, 1, 2, 3, 4];
permutation SHADOW_FILTER_MODE = [0, 1, 2]; // Off, PCF, PCSS
permutation SUPPORTS_EXTENDED_TEXTURES = [0, 1];
permutation EDITOR_MODE = [0, 1]; // Off, Terrain

#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Include/VisibilityBuffers.inc.hlsl"
#include "Include/Editor.inc.hlsl"
#include "Terrain/Shared.inc.hlsl"

// Reenable this in C++ as well
struct Constants
{
	float4 fogColor;
	float4 fogSettings; // x = Enabled, y = Begin Fog Blend Dist, z = End Fog Blend Dist, w = UNUSED
	float4 mouseWorldPos;
	float4 brushSettings; // x = hardness, y = radius, z = pressure, w = falloff
	float4 chunkEdgeColor;
	float4 cellEdgeColor;
	float4 patchEdgeColor;
	float4 vertexColor;
	float4 brushColor;
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float4> _skyboxColor;
[[vk::binding(4, PER_PASS)]] Texture2D<float4> _transparency;
[[vk::binding(5, PER_PASS)]] Texture2D<float> _transparencyWeights;
[[vk::binding(6, PER_PASS)]] RWTexture2D<float4> _resolvedColor;

float4 ShadeTerrain(const uint2 pixelPos, const float2 screenUV, const VisibilityBuffer vBuffer, out float3 outPixelWorldPos)
{
	InstanceData cellInstance = _instanceDatas[vBuffer.drawID];
	uint globalCellID = cellInstance.globalCellID;

	// Terrain code
	uint globalVertexOffset = globalCellID * NUM_VERTICES_PER_CELL;
	uint3 localVertexIDs = GetLocalTerrainVertexIDs(vBuffer.triangleID);

	const uint cellID = cellInstance.packedChunkCellID & 0xFFFF;
	const uint chunkID = cellInstance.packedChunkCellID >> 16;

	// Get vertices
	TerrainVertex vertices[3];

	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		vertices[i] = LoadTerrainVertex(chunkID, cellID, globalVertexOffset, localVertexIDs[i]);
	}

	// Interpolate vertex attributes
	FullBary2 pixelUV = CalcFullBary2(vBuffer.barycentrics, vertices[0].uv, vertices[1].uv, vertices[2].uv); // [0..8] This is correct for terrain color textures
	FullBary3 pixelWorldPosition = CalcFullBary3(vBuffer.barycentrics, vertices[0].position, vertices[1].position, vertices[2].position);

	outPixelWorldPos = pixelWorldPosition.value;
	
	//Camera mainCamera = _cameras[0];
	//float4 pixelViewPosition = mul(float4(pixelWorldPosition.value, 1.0f), mainCamera.worldToView);
	uint shadowCascadeIndex = 0;// GetShadowCascadeIndexFromDepth(pixelViewPosition.z, _constants.numCascades);
#if DEBUG_ID == 4
	return float4(IDToColor3(shadowCascadeIndex + 1), 1.0f);
#endif
	//ViewData shadowCascadeViewData = _shadowCascadeViewDatas[shadowCascadeIndex];

	//float4 pixelShadowPosition = mul(float4(pixelWorldPosition.value, 1.0f), shadowCascadeViewData.viewProjectionMatrix);
	float4 pixelShadowPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);

	float3 pixelColor = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].color, vertices[1].color, vertices[2].color);
	float3 pixelNormal = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].normal, vertices[1].normal, vertices[2].normal);
	float3 pixelAlphaUV = float3(saturate(pixelUV.value / 8.0f), float(cellID)); // However the alpha needs to be between 0 and 1, and load from the correct layer

	// Get CellData and ChunkData
	const CellData cellData = LoadCellData(globalCellID);

	const uint globalChunkID = globalCellID / NUM_CELLS_PER_CHUNK;
	const ChunkData chunkData = _chunkData[globalChunkID];

	// We have 4 uints per chunk for our diffuseIDs, this gives us a size and alignment of 16 bytes which is exactly what GPUs want
	// However, we need a fifth uint for alphaID, so we decided to pack it into the LAST diffuseID, which gets split into two uint16s
	// This is what it looks like
	// [1111] diffuseIDs.x
	// [2222] diffuseIDs.y
	// [3333] diffuseIDs.z
	// [AA44] diffuseIDs.w Alpha is read from the most significant bits, the fourth diffuseID read from the least 
	uint diffuse0ID = cellData.diffuseIDs.x;
	uint diffuse1ID = cellData.diffuseIDs.y;
	uint diffuse2ID = cellData.diffuseIDs.z;
	uint diffuse3ID = cellData.diffuseIDs.w;
	uint alphaID = chunkData.alphaID;

	float3 alpha = _terrainAlphaTextures[NonUniformResourceIndex(alphaID)].SampleGrad(_alphaSampler, pixelAlphaUV, pixelUV.ddx, pixelUV.ddy).rgb;
	float minusAlphaBlendSum = (1.0 - clamp(alpha.x + alpha.y + alpha.z, 0.0, 1.0));
	float4 weightsVector = float4(minusAlphaBlendSum, alpha);

	float4 color = float4(0, 0, 0, 1);

	float3 diffuse0 = _terrainColorTextures[NonUniformResourceIndex(diffuse0ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.x;
	color.rgb += diffuse0;

	float3 diffuse1 = _terrainColorTextures[NonUniformResourceIndex(diffuse1ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.y;
	color.rgb += diffuse1;

	float3 diffuse2 = _terrainColorTextures[NonUniformResourceIndex(diffuse2ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.z;
	color.rgb += diffuse2;

	float3 diffuse3 = _terrainColorTextures[NonUniformResourceIndex(diffuse3ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.w;
	color.rgb += diffuse3;

	// Apply Vertex Color
	color.rgb *= pixelColor;

	/*if (_constants.numTextureDecals + _constants.numProceduralDecals > 0)
	{
		// Apply decals
		ApplyDecals(pixelWorldPosition, color, pixelNormal, _constants.numTextureDecals, _constants.numProceduralDecals, _constants.mouseWorldPos.xyz);
	}*/

	// Apply lighting
	float3 normal = normalize(pixelNormal);
	//float ambientOcclusion = _ambientOcclusion.Load(float3(pixelPos, 0)).x;
	//color.rgb = Lighting(color.rgb, float3(0.0f, 0.0f, 0.0f), normal, ambientOcclusion, true, screenUV, pixelShadowPosition, _constants.shadowFilterSize, _constants.shadowPenumbraFilterSize, shadowCascadeIndex, _constants.enabledShadows);

#if EDITOR_MODE == 1
	// Get settings from push constants
	const float brushHardness = _constants.brushSettings.x;
	const float brushRadius = _constants.brushSettings.y;
	const float brushPressure = _constants.brushSettings.z;
	const float brushFalloff = _constants.brushSettings.w;

	const float wireframeMaxDistance = 533.0f;
	const float wireframeFalloffDistance = 100.0f;

	// Wireframes
	float4 pixelClipSpacePosition = mul(float4(pixelWorldPosition.value, 1.0f), _cameras[0].worldToClip);
	pixelClipSpacePosition.xyz /= pixelClipSpacePosition.w;

	float4 verticesClipSpacePosition[3];
	[unroll]
	for (int i = 0; i < 3; i++)
	{
		verticesClipSpacePosition[i] = mul(float4(vertices[i].position, 1.0f), _cameras[0].worldToClip);
		verticesClipSpacePosition[i].xyz /= verticesClipSpacePosition[i].w;
	}

	// Account for distance to camera
	float distanceFromPixelToCamera = length(pixelWorldPosition.value.xz - _cameras[0].eyePosition.xz);
	float falloff = saturate((wireframeMaxDistance - distanceFromPixelToCamera) / wireframeFalloffDistance);

	// Patches
	float patchEdgeWireframe = WireframeEdge(pixelClipSpacePosition.xy, verticesClipSpacePosition[1].xy, verticesClipSpacePosition[2].xy);
	patchEdgeWireframe *= falloff;

	// Cells
	bool isLeftCellEdge = localVertexIDs[1] % 17 == 0 && localVertexIDs[2] % 17 == 0;
	bool isRightCellEdge = localVertexIDs[1] % 17 == 8 && localVertexIDs[2] % 17 == 8;
	bool isTopCellEdge = localVertexIDs[1] <= 8 && localVertexIDs[2] <= 8;
	bool isBottomCellEdge = localVertexIDs[1] >= 136 && localVertexIDs[2] >= 136;

	bool isCellEdge = isLeftCellEdge || isRightCellEdge || isTopCellEdge || isBottomCellEdge;

	// Chunks
	bool isLeftChunkEdge = cellID % 16 == 0;
	bool isRightChunkEdge = cellID % 16 == 15;
	bool isTopChunkEdge = cellID < 16;
	bool isBottomChunkEdge = cellID >= 240;
	
	bool isChunkEdge = (isLeftChunkEdge && isLeftCellEdge) ||
						(isRightChunkEdge && isRightCellEdge) ||
						(isTopChunkEdge && isTopCellEdge) ||
						(isBottomChunkEdge && isBottomCellEdge);

	if (isChunkEdge)
	{
		// Chunk edge wireframe
		color.rgb = lerp(color.rgb, _constants.chunkEdgeColor.rgb, patchEdgeWireframe);
	}
	else if (isCellEdge)
	{
		// Cell edge wireframe
		color.rgb = lerp(color.rgb, _constants.cellEdgeColor.rgb, patchEdgeWireframe);
	}
	else
	{
		// Patch edge wireframe
		color.rgb = lerp(color.rgb, _constants.patchEdgeColor.rgb, patchEdgeWireframe);
	}

	// Wireframe triangle corners
	float distanceToMouse = distance(_constants.mouseWorldPos.xz, pixelWorldPosition.value.xz);
	if (distanceToMouse < brushRadius)
	{
		float corner = WireframeTriangleCorners(pixelClipSpacePosition.xyz, verticesClipSpacePosition[0].xyz, verticesClipSpacePosition[1].xyz, verticesClipSpacePosition[2].xyz);

		color.rgb = lerp(color.rgb, _constants.vertexColor.rgb, corner);
	}

	// Editor Brush
	float brush = EditorCircleBrush(_constants.mouseWorldPos.xyz, pixelWorldPosition.value.xyz, brushRadius, brushFalloff, vBuffer.barycentrics);

	color.rgb = lerp(_constants.brushColor.rgb, color.rgb, brush);
#endif

	return saturate(color);
}

float4 ShadeModel(const uint2 pixelPos, const float2 screenUV, const VisibilityBuffer vBuffer, out float3 outPixelWorldPos)
{
	ModelDrawCallData drawCallData = LoadModelDrawCallData(vBuffer.drawID);
	ModelInstanceData instanceData = _modelInstanceDatas[drawCallData.instanceID];
	float4x4 instanceMatrix = _modelInstanceMatrices[drawCallData.instanceID];

	// Get the VertexIDs of the triangle we're in
	IndexedDraw draw = _modelDraws[vBuffer.drawID];
	uint3 vertexIDs = GetVertexIDs(vBuffer.triangleID, draw, _modelIndices);

	// Get Vertices
	ModelVertex vertices[3];

	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		vertices[i] = LoadModelVertex(vertexIDs[i]);

		// Animate the vertex normal if we need to
		if (instanceData.boneMatrixOffset != 4294967295)
		{
			// Calculate bone transform matrix
			float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertices[i]);
			vertices[i].normal = mul(vertices[i].normal, (float3x3)boneTransformMatrix);
		}

		// Convert normals to world normals
		vertices[i].normal = mul(vertices[i].normal, (float3x3)instanceMatrix);
	}

	// Interpolate vertex attributes
	FullBary2 pixelUV0 = CalcFullBary2(vBuffer.barycentrics, vertices[0].uv01.xy, vertices[1].uv01.xy, vertices[2].uv01.xy);
	FullBary2 pixelUV1 = CalcFullBary2(vBuffer.barycentrics, vertices[0].uv01.zw, vertices[1].uv01.zw, vertices[2].uv01.zw);

	float3 pixelVertexPosition = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].position, vertices[1].position, vertices[2].position);
	float3 pixelWorldPosition = mul(float4(pixelVertexPosition, 1.0f), instanceMatrix).xyz;

	outPixelWorldPos = pixelWorldPosition;
	
	float4 pixelViewPosition = mul(float4(pixelWorldPosition, 1.0f), _cameras[0].worldToView);
	uint shadowCascadeIndex = 0;// GetShadowCascadeIndexFromDepth(pixelViewPosition.z, _constants.numCascades);
#if DEBUG_ID == 4
	return float4(IDToColor3(shadowCascadeIndex + 1), 1.0f);
#endif
	//ViewData shadowCascadeViewData = _shadowCascadeViewDatas[shadowCascadeIndex];

	//float4 pixelShadowPosition = mul(float4(pixelWorldPosition, 1.0f), shadowCascadeViewData.viewProjectionMatrix);
	float4 pixelShadowPosition = float4(0, 0, 0, 0);

	float3 pixelNormal = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].normal, vertices[1].normal, vertices[2].normal);

	// Shade
	float4 color = float4(0, 0, 0, 0);
	float3 specular = float3(0, 0, 0);
	bool isUnlit = false;

	for (uint textureUnitIndex = drawCallData.textureUnitOffset; textureUnitIndex < drawCallData.textureUnitOffset + drawCallData.numTextureUnits; textureUnitIndex++)
	{
		ModelTextureUnit textureUnit = _modelTextureUnits[textureUnitIndex];

		uint isProjectedTexture = textureUnit.data1 & 0x1;
		uint materialFlags = (textureUnit.data1 >> 1) & 0x3FF;
		uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

		uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;
		uint vertexShaderId = materialType & 0xFF;
		uint pixelShaderId = materialType >> 8;

		if (materialType == 0x8000)
			continue;

		float4 texture0Color = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].SampleGrad(_sampler, pixelUV0.value, pixelUV0.ddx, pixelUV0.ddy);
		float4 texture1Color = float4(0, 0, 0, 0);

		if (vertexShaderId >= 2)
		{
			// ENV uses generated UVCoords based on camera pos + geometry normal in frame space
			texture1Color = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].SampleGrad(_sampler, pixelUV1.value, pixelUV1.ddx, pixelUV1.ddy);
		}

		isUnlit |= (materialFlags & 0x1);

		float4 shadedColor = ShadeModel(pixelShaderId, texture0Color, texture1Color, specular);
		color = BlendModel(blendingMode, color, shadedColor);
	}

	// Apply lighting
	//float ambientOcclusion = _ambientOcclusion.Load(float3(pixelPos, 0)).x;
	//color.rgb = Lighting(color.rgb, float3(0.0f, 0.0f, 0.0f), pixelNormal, ambientOcclusion, !isUnlit, screenUV, pixelShadowPosition, _constants.shadowFilterSize, _constants.shadowPenumbraFilterSize, shadowCascadeIndex, _constants.enabledShadows) + specular;
	//color = float4(pixelUV0.value, 0, 1);
	return saturate(color);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 pixelPos = dispatchThreadId.xy;

	float2 dimensions;
	_resolvedColor.GetDimensions(dimensions.x, dimensions.y);

	if (any(pixelPos > dimensions))
	{
		return;
	}

	uint4 vBufferData = LoadVisibilityBuffer(pixelPos);
	const VisibilityBuffer vBuffer = UnpackVisibilityBuffer(vBufferData);

#if DEBUG_ID != 0
	if (vBuffer.typeID != ObjectType::Terrain && vBuffer.typeID != ObjectType::ModelOpaque)
	{
		_resolvedColor[pixelPos] = float4(0, 0, 0, 1);
		return;
	}
#endif

#if DEBUG_ID == 1 // TypeID debug output
	float3 debugColor = IDToColor3(vBuffer.typeID);
	_resolvedColor[pixelPos] = float4(debugColor, 1);
	return;
#elif DEBUG_ID == 2 // ObjectID debug output
	float3 debugColor = IDToColor3(GetObjectID(vBuffer.typeID, vBuffer.drawID));
	_resolvedColor[pixelPos] = float4(debugColor, 1);
	return;
#elif DEBUG_ID == 3 // TriangleID   
	float3 debugColor = IDToColor3(vBuffer.triangleID);
	_resolvedColor[pixelPos] = float4(debugColor, 1);
	return;
#endif

	float2 pixelUV = pixelPos / dimensions;

	float4 color = float4(0, 0, 0, 1);
	
	float3 pixelWorldPos = _cameras[0].eyePosition.xyz;
	if (vBuffer.typeID == ObjectType::Skybox)
	{
		color = _skyboxColor.Load(int3(pixelPos, 0));
	}
	else if (vBuffer.typeID == ObjectType::JoltDebug)
	{
		// These are passthrough and only write a color packed into vBufferData.y
		color = PackedUnormsToFloat4(vBufferData.y);
	}
	else if (vBuffer.typeID == ObjectType::Terrain)
	{
		color = ShadeTerrain(pixelPos, pixelUV, vBuffer, pixelWorldPos);
	}
	else if (vBuffer.typeID == ObjectType::ModelOpaque) // Transparent models are not rendered using visibility buffers
	{
		color = ShadeModel(pixelPos, pixelUV, vBuffer, pixelWorldPos);
	}
	else
	{
		color.rg = vBuffer.barycentrics.bary;
	}

	// Composite Transparency
	float4 transparency = _transparency.Load(uint3(pixelPos, 0));
	float transparencyWeight = _transparencyWeights.Load(uint3(pixelPos, 0));

	float3 transparencyColor = transparency.rgb / max(transparency.a, 1e-5);

	// Src: ONE_MINUS_SRC_ALPHA, Dst: SRC_ALPHA
	color.rgb = (transparencyColor.rgb * (1.0f - transparencyWeight)) + (color.rgb * transparencyWeight);

	float3 cameraWorldPos = _cameras[0].eyePosition.xyz;
	float distToPixel = distance(cameraWorldPos, pixelWorldPos);

	float fogIntensity = (distToPixel - _constants.fogSettings.y) / (_constants.fogSettings.z - _constants.fogSettings.y);
	fogIntensity = clamp(fogIntensity, 0, 1) * _constants.fogSettings.x;
	color.rgb = lerp(color.rgb, _constants.fogColor.rgb, fogIntensity);
	
	_resolvedColor[pixelPos] = float4(color.rgb, 1.0f);
}