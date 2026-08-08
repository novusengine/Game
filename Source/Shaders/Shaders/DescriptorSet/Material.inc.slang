#ifndef MATERIAL_SET_INCLUDED
#define MATERIAL_SET_INCLUDED

#include "Material/MaterialABI.inc.slang"
#include "Material/MaterialShared.inc.slang"

#define MATERIAL_MAX_SAMPLERS 4u

[[vk::binding(0, MATERIAL)]] StructuredBuffer<MaterialRecord> _materials;
[[vk::binding(1, MATERIAL)]] StructuredBuffer<MaterialInstanceRecord> _materialInstances;
[[vk::binding(2, MATERIAL)]] ByteAddressBuffer _materialParameters;
[[vk::binding(3, MATERIAL)]] SamplerState _materialSampler0;
[[vk::binding(4, MATERIAL)]] SamplerState _materialSampler1;
[[vk::binding(5, MATERIAL)]] SamplerState _materialSampler2;
[[vk::binding(6, MATERIAL)]] SamplerState _materialSampler3;
[[vk::binding(7, MATERIAL)]] StructuredBuffer<uint> _materialTextureIndices;
[[vk::binding(8, MATERIAL)]] StructuredBuffer<uint> _materialSamplerIDs;
[[vk::binding(9, MATERIAL)]] Texture2D<float4> _materialTextures[];

#endif
