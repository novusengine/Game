#ifndef MODEL_SET_INCLUDED
#define MODEL_SET_INCLUDED

#include "Include/Common.inc.slang"

#define MAX_MODEL_SAMPLERS 4

// WARNING: For now, when you change this file, make sure you recompile ALL shaders or you might get descriptor set reflection issues
// This will be fixed later when we have better dependency tracking

[[vk::binding(0, MODEL)]] StructuredBuffer<uint> _opaqueCulledInstanceLookupTable; // One uint per instance, contains instanceRefID of what survives culling, and thus can get reordered
[[vk::binding(1, MODEL)]] StructuredBuffer<InstanceRef> _opaqueInstanceRefTable;
[[vk::binding(2, MODEL)]] StructuredBuffer<PackedTextureData> _packedModelTextureDatas;
[[vk::binding(3, MODEL)]] StructuredBuffer<PackedModelVertex> _packedModelVertices;
[[vk::binding(4, MODEL)]] StructuredBuffer<ModelInstanceData> _modelInstanceDatas;
[[vk::binding(5, MODEL)]] StructuredBuffer<float4x4> _modelInstanceMatrices;
[[vk::binding(6, MODEL)]] StructuredBuffer<float4x4> _instanceBoneMatrices;
[[vk::binding(7, MODEL)]] StructuredBuffer<float4x4> _instanceTextureTransformMatrices;
[[vk::binding(8, MODEL)]] RWStructuredBuffer<PackedAnimatedVertexPosition> _animatedModelVertexPositions;
[[vk::binding(9, MODEL)]] StructuredBuffer<IndexedDraw> _modelDraws;
[[vk::binding(10, MODEL)]] StructuredBuffer<uint> _modelIndices;
[[vk::binding(11, MODEL)]] StructuredBuffer<ModelTextureUnit> _modelTextureUnits;
[[vk::binding(12, MODEL)]] SamplerState _samplers[MAX_MODEL_SAMPLERS];
[[vk::binding(13, MODEL)]] Texture2D<float4> _modelTextures[]; // IMPORTANT: This has to be the last element in this set!

#endif // MODEL_SET_INCLUDED