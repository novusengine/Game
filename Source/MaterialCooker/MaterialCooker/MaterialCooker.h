#pragma once

#include "MaterialCookPlan.h"
#include "MaterialPipelineMeasurement.h"

namespace MaterialCooking
{
    struct MaterialCookRequest
    {
        std::span<const AuthoredMaterialProgramView> sourcePrograms;
        std::span<const MaterialProgramAssignment> assignments;
        MaterialMeasurementMode measurementMode = MaterialMeasurementMode::Disabled;
        MaterialPipelineMeasurementAdapter* measurementAdapter = nullptr;
        bool requireCompleteSourceSemantics = true;
    };

    struct MaterialCookResult
    {
        MaterialCookPlan plan;
        MaterialPipelineMeasurementResult measurement;
        bool functionalCookSucceeded = false;

        explicit operator bool() const
        {
            return functionalCookSucceeded && measurement.measurementRequirementMet;
        }
    };

    // Coordinates the CPU-side functional Material cook and its optional GPU advisory measurement.
    // Keeping the policy here gives standalone tools one success rule regardless of Vulkan availability.
    class MaterialCooker
    {
      public:
        static MaterialCookResult Cook(const MaterialCookRequest& request);
    };
}
