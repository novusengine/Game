#pragma once

#include "MaterialSourceRegistry.h"

namespace MaterialCooking
{
    class AuthoredMaterialAssetWriter
    {
      public:
        static bool Save(const std::filesystem::path& outputDirectory, const MaterialSourceRegistry& registry, const MaterialCookPlan& plan, std::string& error);
    };
}
