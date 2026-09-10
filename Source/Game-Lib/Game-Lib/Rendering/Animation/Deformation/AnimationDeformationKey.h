#pragma once

#include <Base/Types.h>
#include <cstddef>

namespace Animation
{
    // Identifies deformation work by its complete pose and model binding.
    // Hash collisions must not allow different models to share an output table.
    struct DeformationKey
    {
        u64 poseSource = 0;
        u64 poseGeneration = 0;
        u32 model = 0;
        u32 lod = 0;
        bool operator==(const DeformationKey&) const = default;
    };

    struct DeformationKeyHash
    {
        size_t operator()(const DeformationKey& key) const
        {
            size_t result = static_cast<size_t>(key.poseSource ^ (key.poseSource >> 32u));
            result ^= static_cast<size_t>(key.poseGeneration + 0x9E3779B97F4A7C15ull + (result << 6u) + (result >> 2u));
            result ^= static_cast<size_t>(key.model) * 0x9E3779B1u;
            result ^= static_cast<size_t>(key.lod) * 0x85EBCA77u;
            return result;
        }
    };
}
