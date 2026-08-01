#include "AssetValidation.h"

#include <Base/CVarSystem/CVarSystem.h>

namespace
{
    AutoCVar_Int CVAR_ModelValidateAssets(CVarCategory::Client | CVarCategory::Rendering, "modelValidateAssets",
                                          "Perform full semantic validation when loading model and material assets", 0, CVarFlags::EditCheckbox);
}

namespace AssetLoading
{
    bool ShouldPerformFullValidation(ValidationMode mode)
    {
        if (mode == ValidationMode::Full)
            return true;
        if (mode == ValidationMode::Minimal)
            return false;

        return CVAR_ModelValidateAssets.Get() != 0;
    }
} // namespace AssetLoading
