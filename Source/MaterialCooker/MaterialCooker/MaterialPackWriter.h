#pragma once

#include "MaterialCookPlan.h"

#include <filesystem>
#include <string>

namespace MaterialCooking
{
    // Writes the CPU-side cooked program routing and reflection library consumed by Game.
    // Its shader hashes join the pack to permutations emitted by the ordinary shader cooker.
    class MaterialPackWriter
    {
      public:
        static bool Save(const std::filesystem::path& path, const MaterialCookPlan& plan,
                         std::string& error);
    };
}
