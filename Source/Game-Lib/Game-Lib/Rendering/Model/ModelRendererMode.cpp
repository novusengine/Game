#include "ModelRendererMode.h"

#include <Base/CVarSystem/CVarSystem.h>

namespace
{
    AutoCVar_Int CVAR_ModelRendererMode(
        CVarCategory::Client | CVarCategory::Rendering, "modelRendererMode",
        "Select model renderer for map placements (0 legacy, 1 meshlet)", 0);
}

namespace ModelRendering
{
    bool UseMeshletModelRenderer()
    {
        return CVAR_ModelRendererMode.Get() == 1;
    }
}
