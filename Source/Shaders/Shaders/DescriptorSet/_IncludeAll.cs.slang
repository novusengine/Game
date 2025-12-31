
// This file includes all descriptor set includes and provides a dummy compute shader that we can use to init our descriptor sets and bind them.
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

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId: SV_DispatchThreadID)
{
    // This is a placeholder compute shader that does nothing except for including all descriptor set files.
}