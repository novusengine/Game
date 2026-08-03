#pragma once

namespace AssetLoading
{
    enum class ValidationMode
    {
        Default,
        Minimal,
        Full
    };

    enum class FailureInjection
    {
        Texture = 1,
        Material,
        MaterialInstance,
        Model
    };

    bool ShouldPerformFullValidation(ValidationMode mode);
    bool ShouldInjectFailure(FailureInjection type);
} // namespace AssetLoading
