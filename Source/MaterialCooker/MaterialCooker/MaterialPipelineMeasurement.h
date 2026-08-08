#pragma once

#include "MaterialCookPlan.h"

#include <span>
#include <string>
#include <vector>

namespace MaterialCooking
{
    enum class MaterialMeasurementMode : u8
    {
        Disabled,
        Advisory,
        Required
    };

    enum class MaterialMeasurementStatus : u8
    {
        Disabled,
        Available,
        Unavailable
    };

    struct MaterialPipelineStatistic
    {
        std::string canonicalKey;
        std::string name;
        u16 executionGroupID = 0;
        u16 groupLocalProgramID = 0;
        f64 value = 0.0;
        std::string description;
        std::string format;
    };

    struct MaterialPipelineStatisticMedian
    {
        std::string name;
        u16 executionGroupID = 0;
        f64 value = 0.0;
    };

    struct MaterialPipelineStatisticOutlier
    {
        std::string name;
        u16 executionGroupID = 0;
        f64 value = 0.0;
        f64 crossGroupMedian = 0.0;
        f64 ratio = 0.0;
    };

    struct MaterialPipelineNearIdenticalGroups
    {
        u16 firstExecutionGroupID = 0;
        u16 secondExecutionGroupID = 0;
        f64 maximumRelativeDifference = 0.0;
    };

    struct MaterialPipelineBudgetWarning
    {
        std::string name;
        u16 executionGroupID = 0;
        f64 value = 0.0;
        f64 budget = 0.0;
    };

    struct MaterialPipelineMeasurementReport
    {
        MaterialMeasurementStatus status = MaterialMeasurementStatus::Disabled;
        std::string deviceName;
        std::string driverIdentity;
        std::string unavailableReason;
        std::vector<MaterialPipelineStatistic> statistics;
        std::vector<MaterialPipelineStatisticMedian> groupMedians;
        std::vector<MaterialPipelineStatisticOutlier> outliers;
        std::vector<MaterialPipelineNearIdenticalGroups> nearIdenticalGroups;
        std::vector<MaterialPipelineBudgetWarning> budgetWarnings;
    };

    // Supplies optional GPU measurements to the CPU-side cooker without coupling it to Game's Renderer.
    // A narrow adapter lets headless Vulkan disappear cleanly when the platform cannot provide statistics.
    class MaterialPipelineMeasurementAdapter
    {
      public:
        virtual ~MaterialPipelineMeasurementAdapter() = default;
        virtual MaterialPipelineMeasurementReport Measure(
            std::span<const CookedMaterialProgram> programs) = 0;
    };

    struct MaterialPipelineMeasurementResult
    {
        MaterialPipelineMeasurementReport report;
        bool measurementRequirementMet = true;
    };

    // Applies the offline measurement policy after functional Material cooking has completed.
    // Advisory failures never change a successful functional cook into a failed cook.
    class MaterialPipelineMeasurement
    {
      public:
        static MaterialPipelineMeasurementResult Run(
            MaterialMeasurementMode mode, std::span<const CookedMaterialProgram> programs,
            MaterialPipelineMeasurementAdapter* adapter);

      private:
        static void Analyze(MaterialPipelineMeasurementReport& report);
    };
}
