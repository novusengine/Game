#pragma once

#include "MaterialCookPlan.h"

#include <filesystem>
#include <string>

namespace MaterialCooking
{
    // Assembles authored GPU-side Material functions and compact program tables per execution group.
    // Function dispatch preserves authored behavior while one generic path handles every layered composition.
    class AuthoredMaterialShaderGenerator
    {
      public:
        static std::string GenerateGroupSource(const MaterialCookPlan& plan, u16 executionGroupID);
        static bool Generate(const std::filesystem::path& directory, const MaterialCookPlan& plan);
    };
}
