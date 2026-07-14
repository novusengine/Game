#ifndef LIGHT_SET_INCLUDED
#define LIGHT_SET_INCLUDED

#ifndef MAX_SHADOW_CASCADES
#define MAX_SHADOW_CASCADES 8 // Has to be kept in sync with the one in RenderSettings.h
#endif

#include "Shadows/SVSM.inc.slang"

// WARNING: For now, when you change this file, make sure you recompile ALL shaders or you might get descriptor set reflection issues
// This will be fixed later when we have better dependency tracking

[[vk::binding(0, LIGHT)]] StructuredBuffer<uint> _entityTiles;
[[vk::binding(1, LIGHT)]] SamplerComparisonState _shadowCmpSampler;
[[vk::binding(2, LIGHT)]] SamplerState _shadowPointClampSampler;
[[vk::binding(3, LIGHT)]] Texture2D<float> _shadowCascadeRTs[MAX_SHADOW_CASCADES];
[[vk::binding(4, LIGHT)]] StructuredBuffer<PackedDecal> _packedDecals; // All decals in the world

// SVSM (shadowTechnique 1) sampling resources
[[vk::binding(5, LIGHT)]] StructuredBuffer<SVSMData> _svsmData;
[[vk::binding(6, LIGHT)]] StructuredBuffer<uint> _svsmPageTable;
[[vk::binding(7, LIGHT)]] Texture2D<uint> _svsmPagePool;
[[vk::binding(8, LIGHT)]] StructuredBuffer<uint> _svsmDynamicPageTable;
[[vk::binding(9, LIGHT)]] Texture2D<uint> _svsmDynamicPagePool;

#endif // LIGHT_SET_INCLUDED