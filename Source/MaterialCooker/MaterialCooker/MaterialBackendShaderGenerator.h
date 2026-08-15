#pragma once

#include "MaterialCookPlan.h"

#include <filesystem>
#include <string>

namespace MaterialCooking
{
    // Generates GPU-side fragment entry points around cooked Material execution groups.
    // Separate coverage and forward roots prove the authored functions under their real shader stages.
    class MaterialBackendShaderGenerator
    {
      public:
        static std::string GenerateCoverageSource(const MaterialCookPlan& plan);
        static std::string GenerateDirectForwardSource(const MaterialCookPlan& plan);
        static std::string GenerateForwardSource(const MaterialCookPlan& plan);
        static std::string GenerateSelectionSource(const MaterialCookPlan& plan);
        static bool Generate(const std::filesystem::path& directory,
                             const MaterialCookPlan& plan);
    };
}
