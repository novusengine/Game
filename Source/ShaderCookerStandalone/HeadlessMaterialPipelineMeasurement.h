#pragma once

#include <MaterialCooker/MaterialPipelineMeasurement.h>

#include <filesystem>

// Measures cooked GPU-side Material group pipelines through a windowless Vulkan device.
// The resulting driver statistics are advisory inputs for manually reviewing execution groups.
class HeadlessMaterialPipelineMeasurement final
    : public MaterialCooking::MaterialPipelineMeasurementAdapter
{
  public:
    explicit HeadlessMaterialPipelineMeasurement(std::filesystem::path shaderPackPath);

    MaterialCooking::MaterialPipelineMeasurementReport Measure(
        std::span<const MaterialCooking::CookedMaterialProgram> programs) override;

  private:
    std::filesystem::path _shaderPackPath;
};
