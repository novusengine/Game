#include "ModelRendererMode.h"

#include <Base/CVarSystem/CVarSystem.h>

namespace
{
    AutoCVar_Int CVAR_ModelRendererMode(
        CVarCategory::Client | CVarCategory::Rendering, "modelRendererMode",
        "Select model renderer for map placements (0 legacy, 1 meshlet)", 0);
    AutoCVar_Int CVAR_ModelCullReasonDebug(
        CVarCategory::Client | CVarCategory::Rendering, "modelCullReasonDebug",
        "Show rejected model meshlets by cull reason (red frustum, orange cone, blue occlusion)", 0,
        CVarFlags::EditCheckbox);
}

namespace ModelRendering
{
    bool UseMeshletModelRenderer()
    {
        return CVAR_ModelRendererMode.Get() == 1;
    }

    bool ShowModelCullReasons()
    {
        return CVAR_ModelCullReasonDebug.Get() != 0;
    }
}
