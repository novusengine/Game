material Terrain
{
    programFamily = Terrain;
}

#include "Terrain/TerrainShared.inc.slang"

SurfaceDescription EvaluateMaterial(MaterialEvaluationContext context)
{
    uint globalCellID = context.parameterBlockIndex;
    uint cellID = globalCellID % NUM_CELLS_PER_CHUNK;
    uint globalChunkID = globalCellID / NUM_CELLS_PER_CHUNK;
    CellData cell = LoadCellData(globalCellID);
    ChunkData chunk = _chunkData[globalChunkID];

    float3 alphaUV = float3(saturate(context.input.uv0 / 8.0f), float(cellID));
    float3 alpha = _terrainAlphaTextures[NonUniformResourceIndex(chunk.alphaID)].SampleGrad(_alphaSampler, alphaUV, context.input.uv0DDX, context.input.uv0DDY).rgb;
    float4 weights = float4(1.0f - saturate(alpha.x + alpha.y + alpha.z), alpha);
    float3 layer0 = _terrainColorTextures[NonUniformResourceIndex(cell.diffuseIDs.x)].SampleGrad(_sampler, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY).rgb;
    float3 layer1 = _terrainColorTextures[NonUniformResourceIndex(cell.diffuseIDs.y)].SampleGrad(_sampler, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY).rgb;
    float3 layer2 = _terrainColorTextures[NonUniformResourceIndex(cell.diffuseIDs.z)].SampleGrad(_sampler, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY).rgb;
    float3 layer3 = _terrainColorTextures[NonUniformResourceIndex(cell.diffuseIDs.w)].SampleGrad(_sampler, context.input.uv0, context.input.uv0DDX, context.input.uv0DDY).rgb;

    float3 albedo = (layer0 * weights.x + layer1 * weights.y + layer2 * weights.z + layer3 * weights.w) * context.input.vertexColor.rgb;

    SurfaceDescription surface = MakeDefaultSurface(context.input);
    surface.albedo = albedo;
    return surface;
}
