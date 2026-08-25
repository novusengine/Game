permutation DEBUG_ID = [0, 1, 2, 3, 4]; // Off, TypeID, ObjectID, TriangleID, SVSM compare margin
permutation EDITOR_MODE = [0, 1]; // Off, Terrain

#include "DescriptorSet/Debug.inc.slang"
#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Light.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Editor.inc.slang"
#include "Include/Lighting.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Light/LightShared.inc.slang"
#include "Material/Decals.inc.slang"
#include "Material/MaterialAuthoring.inc.slang"
#include "Terrain/TerrainShared.inc.slang"

struct Constants
{
    float4 renderInfo; // x = Render Width, y = Render Height, z = 1/Width, w = 1/Height
    uint4 lightInfo; // x = Num Directional Lights, y = Shadows Ready (enabled, strength > 0, pool created), zw = UNUSED
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
    float4 shadowSettings; // x = Shadow Strength, y = Normal Offset Bias, z = SVSM Constant Bias (world meters), w = UNUSED
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float4> _skyboxColor;
[[vk::binding(6, PER_PASS)]] RWTexture2D<float4> _resolvedColor;

#include "Generated/MaterialSources/Terrain.inc.slang"

float4 ShadeTerrain(const uint2 pixelPos, const float2 screenUV, const VisibilityBuffer vBuffer, uint tileIndex, out float3 outPixelWorldPos)
{
    // Get the interpolated vertex data from the visibility buffer
    PixelVertexData pixelVertexData = GetPixelVertexDataTerrain(pixelPos, vBuffer, 0, _constants.renderInfo.xy);

    InstanceData cellInstance = _instanceDatas[vBuffer.instanceID];
    uint globalCellID = cellInstance.globalCellID;

    ShadowSettings shadowSettings;
    shadowSettings.enableShadows = _constants.lightInfo.y == 1;
    shadowSettings.strength = _constants.shadowSettings.x;
    shadowSettings.normalOffset = _constants.shadowSettings.y;
    shadowSettings.svsmConstantBias = _constants.shadowSettings.z;

    MaterialInput materialInput;
    materialInput.worldPosition = pixelVertexData.worldPos;
    materialInput.geometricNormal = pixelVertexData.worldNormal;
    materialInput.vertexColor = float4(pixelVertexData.color, 1.0f);
    materialInput.uv0 = pixelVertexData.uv0.value;
    materialInput.uv0DDX = pixelVertexData.uv0.ddx;
    materialInput.uv0DDY = pixelVertexData.uv0.ddy;
    materialInput.frontFacing = true;
    MaterialEvaluationContext context;
    context.input = materialInput;
    context.instance.parameterOffset = 0u;
    context.instance.materialIndex = 0u;
    context.instance.textureOffset = 0u;
    context.instance.textureCount = 0u;
    context.instance.flags = 0u;
    context.instance.packedClassification = 0u;
    context.instance.materialHandle = 0u;
    context.instance.samplerOffset = 0u;
    context.textureOffset = 0u;
    context.parameterBlockIndex = globalCellID;
    SurfaceDescription surface = EvaluateMaterial_Terrain(context);
    ApplyDecals(tileIndex, pixelVertexData.worldPos, pixelVertexData.worldNormal, surface);
    LightingModelInput lightingInput = BuildLightingModelInput(pixelPos, 0u, pixelVertexData.worldPos, pixelVertexData.worldNormal, pixelVertexData.faceNormal, _constants.lightInfo.x, true, shadowSettings);
    float4 color = float4(EvaluateLightingModel(LIGHTING_MODEL_STANDARD, surface, lightingInput), surface.alpha);

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

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadID.xy;

    if (any(pixelPos >= uint2(_constants.renderInfo.xy)))
    {
        return;
    }

    uint vBufferData = LoadVisibilityBuffer(pixelPos);
    if (IsModelVisibility(vBufferData))
        return;
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
#elif DEBUG_ID == 4 // SVSM compare margin
    PixelVertexData pixelVertexData = GetPixelVertexData(pixelPos, vBuffer, 0, _constants.renderInfo.xy);
    float3 debugColor = GetSVSMDebugColor(pixelVertexData.worldPos, pixelVertexData.worldNormal, _constants.shadowSettings.y, _constants.shadowSettings.z);
    _resolvedColor[pixelPos] = float4(debugColor, 1);
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
        color = DecodeJoltDebugColor(vBufferData);
    }
    else if (vBuffer.typeID == ObjectType::Terrain)
    {
        color = ShadeTerrain(pixelPos, pixelUV, vBuffer, tileIndex, pixelWorldPos);
    }
    else
    {
        color.rg = float2(1, 1);
    }

    float3 cameraWorldPos = _cameras[0].eyePosition.xyz;
    float distToPixel = distance(cameraWorldPos, pixelWorldPos);

    float fogIntensity = (distToPixel - _constants.fogSettings.y) / (_constants.fogSettings.z - _constants.fogSettings.y);
    fogIntensity = clamp(fogIntensity, 0, 1) * _constants.fogSettings.x;
    color.rgb = lerp(color.rgb, _constants.fogColor.rgb, fogIntensity);
    
    _resolvedColor[pixelPos] = float4(color.rgb, 1.0f);
}
