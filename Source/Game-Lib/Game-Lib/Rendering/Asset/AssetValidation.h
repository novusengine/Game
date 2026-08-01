#pragma once

namespace AssetLoading
{
    enum class ValidationMode
    {
        Default,
        Minimal,
        Full
    };

    bool ShouldPerformFullValidation(ValidationMode mode);
} // namespace AssetLoading
