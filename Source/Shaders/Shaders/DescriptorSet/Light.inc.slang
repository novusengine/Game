#ifndef LIGHT_SET_INCLUDED
#define LIGHT_SET_INCLUDED

#include "Light/LightShared.inc.slang"
#include "Shadows/SVSM.inc.slang"

struct DirectionalLight
{
    float4 direction;
    float4 color;
    float4 groundAmbientColor;
    float4 skyAmbientColor;
    float4 shadowColor;
};

// WARNING: For now, when you change this file, make sure you recompile ALL shaders or you might get descriptor set reflection issues
// This will be fixed later when we have better dependency tracking

[[vk::binding(0, LIGHT)]] StructuredBuffer<uint> _entityTiles;
[[vk::binding(1, LIGHT)]] StructuredBuffer<PackedDecal> _packedDecals; // All decals in the world

// SVSM sampling resources
[[vk::binding(2, LIGHT)]] StructuredBuffer<SVSMData> _svsmData;
[[vk::binding(3, LIGHT)]] StructuredBuffer<uint> _svsmPageTable;
[[vk::binding(4, LIGHT)]] Texture2D<uint> _svsmPagePool;
[[vk::binding(5, LIGHT)]] StructuredBuffer<uint> _svsmDynamicPageTable;
[[vk::binding(6, LIGHT)]] Texture2D<uint> _svsmDynamicPagePool;
[[vk::binding(7, LIGHT)]] StructuredBuffer<DirectionalLight> _directionalLights;

#endif // LIGHT_SET_INCLUDED
