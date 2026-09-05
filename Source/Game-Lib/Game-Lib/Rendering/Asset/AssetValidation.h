#pragma once

namespace AssetLoading
{
    enum class FailureInjection
    {
        Texture = 1,
        Material,
        MaterialInstance,
        Model,
        Animation
    };

    bool ShouldPerformFullValidation();
    bool ShouldInjectFailure(FailureInjection type);
} // namespace AssetLoading
