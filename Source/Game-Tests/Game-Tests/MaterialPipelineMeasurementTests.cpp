#include <MaterialCooker/MaterialPipelineMeasurement.h>

#include <catch2/catch2.hpp>

namespace
{
    class AvailableMeasurementAdapter final
        : public MaterialCooking::MaterialPipelineMeasurementAdapter
    {
      public:
        MaterialCooking::MaterialPipelineMeasurementReport Measure(
            std::span<const MaterialCooking::CookedMaterialProgram>) override
        {
            MaterialCooking::MaterialPipelineMeasurementReport report;
            report.status = MaterialCooking::MaterialMeasurementStatus::Available;
            report.deviceName = "Test Device";
            report.statistics = {
                {"execution-group", "Binary Size", 2, 0, 70000.0},
                {"execution-group", "Register Count", 2, 0, 96.0},
                {"execution-group", "Binary Size", 1, 0, 1000.0},
                {"execution-group", "Register Count", 1, 0, 16.0},
                {"execution-group", "Binary Size", 0, 0, 1020.0},
                {"execution-group", "Register Count", 0, 0, 16.0},
            };
            return report;
        }
    };
}

TEST_CASE("Advisory Material measurement tolerates unavailable Vulkan",
          "[Rendering][MaterialCooker]")
{
    const MaterialCooking::MaterialPipelineMeasurementResult result =
        MaterialCooking::MaterialPipelineMeasurement::Run(
            MaterialCooking::MaterialMeasurementMode::Advisory, {}, nullptr);

    CHECK(result.measurementRequirementMet);
    CHECK(result.report.status == MaterialCooking::MaterialMeasurementStatus::Unavailable);
    CHECK_FALSE(result.report.unavailableReason.empty());
}

TEST_CASE("Required Material measurement fails only its explicit requirement",
          "[Rendering][MaterialCooker]")
{
    auto result = MaterialCooking::MaterialPipelineMeasurement::Run(
        MaterialCooking::MaterialMeasurementMode::Required, {}, nullptr);
    CHECK_FALSE(result.measurementRequirementMet);

    AvailableMeasurementAdapter adapter;
    result = MaterialCooking::MaterialPipelineMeasurement::Run(
        MaterialCooking::MaterialMeasurementMode::Required, {}, &adapter);
    CHECK(result.measurementRequirementMet);
    CHECK(result.report.status == MaterialCooking::MaterialMeasurementStatus::Available);
    REQUIRE(result.report.statistics.size() == 6);
    CHECK(result.report.statistics.front().executionGroupID == 0);
    CHECK(result.report.statistics.back().executionGroupID == 2);
    REQUIRE(result.report.groupMedians.size() == 6);
    REQUIRE(result.report.outliers.size() == 2);
    CHECK(result.report.outliers[0].executionGroupID == 2);
    CHECK(result.report.outliers[1].executionGroupID == 2);
    REQUIRE(result.report.nearIdenticalGroups.size() == 1);
    CHECK(result.report.nearIdenticalGroups[0].firstExecutionGroupID == 0);
    CHECK(result.report.nearIdenticalGroups[0].secondExecutionGroupID == 1);
    REQUIRE(result.report.budgetWarnings.size() == 2);
    CHECK(result.report.budgetWarnings[0].executionGroupID == 2);
    CHECK(result.report.budgetWarnings[1].executionGroupID == 2);
}
