#include "AssetValidation.h"

#include <Base/CVarSystem/CVarSystem.h>

namespace
{
    AutoCVar_Int CVAR_RenderAssetFullValidation(
        CVarCategory::Client | CVarCategory::Rendering, "renderAssetFullValidation",
        "Perform full semantic validation when loading render assets", 0, CVarFlags::EditCheckbox);
    AutoCVar_Int CVAR_RenderAssetFailureInjection(
        CVarCategory::Client | CVarCategory::Rendering, "renderAssetFailureInjection",
        "Inject render asset failures: 0 off, 1 texture, 2 material, 3 material instance, 4 model, 5 animation", 0,
        CVarFlags::DoNotSave);
}

namespace AssetLoading
{
    bool ShouldPerformFullValidation()
    {
        return CVAR_RenderAssetFullValidation.Get() != 0;
    }

    bool ShouldInjectFailure(FailureInjection type)
    {
        return CVAR_RenderAssetFailureInjection.Get() == static_cast<i32>(type);
    }
} // namespace AssetLoading
