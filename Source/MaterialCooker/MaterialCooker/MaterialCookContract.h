#pragma once

#include <Base/Types.h>

namespace MaterialCooking::Contract
{
    // Versions the AssetConverter-owned aggregate input independently from runtime assets.
    inline constexpr u32 PROGRAM_MANIFEST_VERSION = 2;

    // Versions the hand-authored Material function registry.
    inline constexpr u32 AUTHORING_REGISTRY_VERSION = 1;

    // Versions the reviewed execution-group and stable local-index assignments.
    inline constexpr u32 ASSIGNMENT_REGISTRY_VERSION = 1;

    // Versions the diagnostic report without coupling it to any runtime FileFormat.
    inline constexpr u32 COOK_REPORT_VERSION = 1;
}
