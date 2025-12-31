
// This file includes all descriptor set includes and provides a dummy vertex shader that we can use to init our descriptor sets and bind them.
// If you add more descriptor sets, please include them here.

#include "Debug.inc.slang"
#include "Global.inc.slang"
#include "Light.inc.slang"
#include "Model.inc.slang"
#include "Terrain.inc.slang"

// Includes that provide types used by descriptor sets
#include "Light/LightShared.inc.slang"
#include "Model/ModelShared.inc.slang"
#include "Terrain/TerrainShared.inc.slang"

struct VSOut
{
    float4 Position : SV_Position;
};

[shader("vertex")]
VSOut main(uint vertexId: SV_VertexID)
{
    VSOut o;
    o.Position = float4(0.0, 0.0, 0.0, 1.0);
    return o;
}