#ifndef MODEL_SET_INCLUDED
#define MODEL_SET_INCLUDED

#include "Include/Common.inc.hlsl"

#define MAX_MODEL_SAMPLERS 4

[[vk::binding(0, MODEL)]] StructuredBuffer<uint> _culledInstanceLookupTable; // One uint per instance, contains instanceRefID of what survives culling, and thus can get reordered
[[vk::binding(1, MODEL)]] StructuredBuffer<InstanceRef> _instanceRefTable;
[[vk::binding(2, MODEL)]] StructuredBuffer<ModelDrawCallData> _modelDrawCallDatas;
[[vk::binding(3, MODEL)]] StructuredBuffer<PackedTextureData> _packedModelTextureDatas;
[[vk::binding(4, MODEL)]] StructuredBuffer<PackedModelVertex> _packedModelVertices;
[[vk::binding(5, MODEL)]] StructuredBuffer<ModelInstanceData> _modelInstanceDatas;
[[vk::binding(6, MODEL)]] StructuredBuffer<float4x4> _modelInstanceMatrices;
[[vk::binding(7, MODEL)]] StructuredBuffer<float4x4> _instanceBoneMatrices;
[[vk::binding(8, MODEL)]] StructuredBuffer<float4x4> _instanceTextureTransformMatrices;
[[vk::binding(9, MODEL)]] RWStructuredBuffer<PackedAnimatedVertexPosition> _animatedModelVertexPositions;
[[vk::binding(10, MODEL)]] StructuredBuffer<IndexedDraw> _modelDraws;
[[vk::binding(11, MODEL)]] StructuredBuffer<uint> _modelIndices;
[[vk::binding(12, MODEL)]] StructuredBuffer<ModelTextureUnit> _modelTextureUnits;
[[vk::binding(13, MODEL)]] SamplerState _samplers[MAX_MODEL_SAMPLERS];
[[vk::binding(20, MODEL)]] Texture2D<float4> _modelTextures[MAX_TEXTURES]; // We give this index 20 because it always needs to be last in this descriptor set

#endif // MODEL_SET_INCLUDED