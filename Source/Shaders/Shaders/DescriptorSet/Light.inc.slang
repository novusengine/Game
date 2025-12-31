#ifndef LIGHT_SET_INCLUDED
#define LIGHT_SET_INCLUDED

#ifndef MAX_SHADOW_CASCADES
#define MAX_SHADOW_CASCADES 8 // Has to be kept in sync with the one in RenderSettings.h
#endif

// WARNING: For now, when you change this file, make sure you recompile ALL shaders or you might get descriptor set reflection issues
// This will be fixed later when we have better dependency tracking

[[vk::binding(0, LIGHT)]] StructuredBuffer<uint> _entityTiles;
[[vk::binding(1, LIGHT)]] SamplerComparisonState _shadowCmpSampler;
[[vk::binding(2, LIGHT)]] SamplerState _shadowPointClampSampler;
[[vk::binding(3, LIGHT)]] Texture2D<float> _shadowCascadeRTs[MAX_SHADOW_CASCADES];
[[vk::binding(4, LIGHT)]] StructuredBuffer<PackedDecal> _packedDecals; // All decals in the world

#endif // LIGHT_SET_INCLUDED