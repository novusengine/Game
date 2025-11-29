#ifndef LIGHT_SET_INCLUDED
#define LIGHT_SET_INCLUDED

#ifndef MAX_SHADOW_CASCADES
#define MAX_SHADOW_CASCADES 8 // Has to be kept in sync with the one in RenderSettings.h
#endif

[[vk::binding(0, LIGHT)]] SamplerComparisonState _shadowCmpSampler;
[[vk::binding(1, LIGHT)]] SamplerState _shadowPointClampSampler;
[[vk::binding(2, LIGHT)]] Texture2D<float> _shadowCascadeRTs[MAX_SHADOW_CASCADES];
[[vk::binding(3, LIGHT)]] StructuredBuffer<PackedDecal> _packedDecals; // All decals in the world

#endif // LIGHT_SET_INCLUDED