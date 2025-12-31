permutation DEBUG_ID = [0, 1, 2, 3, 4];
permutation SHADOW_FILTER_MODE = [0, 1, 2]; // Off, PCF, PCSS
permutation EDITOR_MODE = [0, 1]; // Off, Terrain

#include "DescriptorSet/Debug.inc.slang"
#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Light.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Editor.inc.slang"
#include "Include/Lighting.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"
#include "Light/LightShared.inc.slang"
#include "Model/ModelShared.inc.slang"
#include "Terrain/TerrainShared.inc.slang"

struct Constants
{
    float4 renderInfo; // x = Render Width, y = Render Height, z = 1/Width, w = 1/Height
    uint4 lightInfo; // x = Num Directional Lights, y = Num Point Lights, z = Num Cascades, w = Shadows Enabled
    uint4 tileInfo; // xy = Num Tiles
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

float3 ProcessDecalBucket(uint buckedIndex, uint mask, float3 pixelWorldPos, float3 pixelWorldNormal)
{
    // Walk set bits in mask
    float3 color = float3(0, 0, 0);

    while (mask)
    {
        uint bit = firstbitlow(mask);
        uint decalIndex = buckedIndex * 32u + bit; // Global index into _decals

        PackedDecal packedDecal = _packedDecals[decalIndex];
        Decal decal = UnpackDecal(packedDecal);

        Texture2D decalTexture = _modelTextures[NonUniformResourceIndex(decal.textureID)]; // Decals share the same texture array as models

        color += ApplyDecal(pixelWorldPos, pixelWorldNormal, decal, decalTexture, _sampler);

        mask &= (mask - 1); // Clear the least significant bit set
    }

    return color;
}

float3 ApplyDecals(uint tileIndex, float3 pixelWorldPos, float3 pixelWorldNormal)
{
    // Layout reminder:
    // [0 .. Tiles*Buckets-1]          : Opaque bitmasks per tile, use these when applied to opaque objects
    // [Tiles*Buckets .. 2*Tiles*Buckets-1] : Transparent bitmasks per tile, use these whne applied to transparent objects such as fog between camera and opaque (not actually used yet)

    uint numTiles = _constants.tileInfo.x * _constants.tileInfo.y;
    uint bucketsPerLayer = SHADER_ENTITY_TILE_BUCKET_COUNT;

    uint opaqueBase = tileIndex * bucketsPerLayer;
    uint transparentBase = numTiles * bucketsPerLayer + opaqueBase; // I don't think I need this but lets keep it so we know what to do in the future

    float3 color = float3(0, 0, 0);
    for (uint bucketIndex = 0; bucketIndex < bucketsPerLayer; bucketIndex++)
    {
        uint mask = _entityTiles[opaqueBase + bucketIndex];
        if (mask != 0)
            color += ProcessDecalBucket(bucketIndex, mask, pixelWorldPos, pixelWorldNormal);
    }

    return saturate(color);
}

float4 ShadeTerrain(const uint2 pixelPos, const float2 screenUV, const VisibilityBuffer vBuffer, uint tileIndex, out float3 outPixelWorldPos)
{
    // Get the interpolated vertex data from the visibility buffer
    PixelVertexData pixelVertexData = GetPixelVertexDataTerrain(pixelPos, vBuffer, 0, _constants.renderInfo.xy);

    InstanceData cellInstance = _instanceDatas[vBuffer.instanceID];
    uint globalCellID = cellInstance.globalCellID;
    const uint cellID = cellInstance.packedChunkCellID & 0xFFFF;

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

    FullBary2 pixelUV = pixelVertexData.uv0;
    float3 pixelAlphaUV = float3(saturate(pixelUV.value / 8.0f), float(cellID)); // The alpha needs to be between 0 and 1, and load from the correct layer

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

    // Apply vertex color
    color.rgb *= pixelVertexData.color;

    // TODO: Don't hardcode this
    ShadowSettings shadowSettings;
    shadowSettings.enableShadows = _constants.lightInfo.w == 1;
    shadowSettings.filterSize = 3.0f;
    shadowSettings.penumbraFilterSize = 3.0f;

    // Apply lighting
    color.rgb = ApplyLighting(screenUV, color.rgb, pixelVertexData, _constants.lightInfo, shadowSettings);

    // Apply decals
    color.rgb += ApplyDecals(tileIndex, pixelVertexData.worldPos, pixelVertexData.worldNormal);
    
/*#if EDITOR_MODE == 1
    // Get settings from push constants
    const float brushHardness = _constants.brushSettings.x;
    const float brushRadius = _constants.brushSettings.y;
    const float brushPressure = _constants.brushSettings.z;
    const float brushFalloff = _constants.brushSettings.w;

    const float wireframeMaxDistance = 533.0f;
    const float wireframeFalloffDistance = 100.0f;

    // Wireframes
    float4 pixelClipSpacePosition = mul(float4(pixelVertexData.worldPos.value, 1.0f), _cameras[0].worldToClip);
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
#endif*/

    outPixelWorldPos = pixelVertexData.worldPos;
    return saturate(color);
}

float4 ShadeModel(const uint2 pixelPos, const float2 screenUV, const VisibilityBuffer vBuffer, uint tileIndex, out float3 outPixelWorldPos)
{
    // Get the interpolated vertex data from the visibility buffer
    PixelVertexData pixelVertexData = GetPixelVertexDataModel(pixelPos, vBuffer, 0, _constants.renderInfo.xy);
    
    TextureData textureData = LoadModelTextureData(pixelVertexData.extraID);

    // Shade
    float4 color = float4(0, 0, 0, 0);
    float3 specular = float3(0, 0, 0);
    bool isUnlit = false;

    for (uint textureUnitIndex = textureData.textureUnitOffset; textureUnitIndex < textureData.textureUnitOffset + textureData.numTextureUnits; textureUnitIndex++)
    {
        ModelTextureUnit textureUnit = _modelTextureUnits[textureUnitIndex];

        uint isProjectedTexture = textureUnit.data1 & 0x1;
        uint texture0SamplerIndex = (textureUnit.data1 >> 1) & 0x3;
        uint texture1SamplerIndex = (textureUnit.data1 >> 3) & 0x3;
        uint materialFlags = (textureUnit.data1 >> 5) & 0x3F;
        uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

        uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;
        uint vertexShaderId = materialType & 0xFF;
        uint pixelShaderId = materialType >> 8;
        uint rgba = textureUnit.rgba;
        float4 textureUnitColor = float4((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF) / 255.0f;

        if (materialType == 0x8000)
            continue;

        float4 texture0Color = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].SampleGrad(_samplers[texture0SamplerIndex], pixelVertexData.uv0.value, pixelVertexData.uv0.ddx, pixelVertexData.uv0.ddy);
        float4 texture1Color = float4(0, 0, 0, 0);

        if (vertexShaderId >= 2)
        {
            // ENV uses generated UVCoords based on camera pos + geometry normal in frame space
            texture1Color = _modelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].SampleGrad(_samplers[texture1SamplerIndex], pixelVertexData.uv1.value, pixelVertexData.uv1.ddx, pixelVertexData.uv1.ddy);
        }

        // We are not sure if these should be applied here, also see DrawTransparent.ps.hlsl
        texture0Color *= textureUnitColor;
        texture1Color *= textureUnitColor;

        isUnlit |= (materialFlags & 0x1);

        float4 shadedColor = ShadeModel(pixelShaderId, texture0Color, texture1Color, specular); // It's also possible that textureUnitColor should be applied on this
        color = BlendModel(blendingMode, color, shadedColor); // Or on this
    }

    // TODO: Don't hardcode this
    ShadowSettings shadowSettings;
    shadowSettings.enableShadows = _constants.lightInfo.w == 1;
    shadowSettings.filterSize = 3.0f;
    shadowSettings.penumbraFilterSize = 3.0f;

    // Apply lighting
    color.rgb = ApplyLighting(screenUV, color.rgb, pixelVertexData, _constants.lightInfo, shadowSettings);

    // Apply highlight
    color.rgb *= pixelVertexData.highlightIntensity;

    // Apply decals
    color.rgb += ApplyDecals(tileIndex, pixelVertexData.worldPos, pixelVertexData.worldNormal);

    outPixelWorldPos = pixelVertexData.worldPos;
    return saturate(color);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadId.xy;

    if (any(pixelPos > _constants.renderInfo.xy))
    {
        return;
    }

    uint2 vBufferData = LoadVisibilityBuffer(pixelPos);
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
    float3 debugColor = IDToColor3(GetObjectID(vBuffer.typeID, vBuffer.instanceID));
    _resolvedColor[pixelPos] = float4(debugColor, 1);
    return;
#elif DEBUG_ID == 3 // TriangleID   
    float3 debugColor = IDToColor3(vBuffer.triangleID);
    _resolvedColor[pixelPos] = float4(debugColor, 1);
    return;
#elif DEBUG_ID == 4 // CascadeID
    uint numCascades = _constants.lightInfo.z;
    PixelVertexData pixelVertexData = GetPixelVertexData(pixelPos, vBuffer, 0, _constants.renderInfo.xy);
    uint cascadeIndex = GetShadowCascadeIndexFromDepth(pixelVertexData.viewPos.z, numCascades);
    _resolvedColor[pixelPos] = float4(CascadeIDToColor(cascadeIndex), 1);
    return;
#endif

    float2 pixelUV = pixelPos / _constants.renderInfo.xy;
    uint2 tileCoord = PixelToTile(pixelPos, TILED_CULLING_BLOCKSIZE);
    uint tileIndex = Flatten2D(tileCoord, _constants.tileInfo.xy);

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
        color = ShadeTerrain(pixelPos, pixelUV, vBuffer, tileIndex, pixelWorldPos);
    }
    else if (vBuffer.typeID == ObjectType::ModelOpaque) // Transparent models are not rendered using visibility buffers
    {
        color = ShadeModel(pixelPos, pixelUV, vBuffer, tileIndex, pixelWorldPos);
    }
    else
    {
        color.rg = float2(1, 1);
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