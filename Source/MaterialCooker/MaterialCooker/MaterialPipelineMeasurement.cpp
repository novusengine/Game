#include "MaterialPipelineMeasurement.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string_view>

namespace
{
    constexpr f64 OUTLIER_RATIO = 1.5;
    constexpr f64 NEAR_IDENTICAL_RELATIVE_DIFFERENCE = 0.05;
    constexpr f64 REGISTER_COUNT_BUDGET = 64.0;
    constexpr f64 BINARY_SIZE_BUDGET = 64.0 * 1024.0;

    f64 Median(std::vector<f64> values)
    {
        std::sort(values.begin(), values.end());
        const size_t middle = values.size() / 2;
        if ((values.size() & 1u) != 0u)
            return values[middle];
        return (values[middle - 1] + values[middle]) * 0.5;
    }

    f64 RelativeDifference(f64 left, f64 right)
    {
        return std::abs(left - right) / std::max({std::abs(left), std::abs(right), 1.0});
    }

    f64 StatisticBudget(std::string_view name)
    {
        if (name == "Register Count")
            return REGISTER_COUNT_BUDGET;
        if (name == "Binary Size")
            return BINARY_SIZE_BUDGET;
        return 0.0;
    }
}

namespace MaterialCooking
{
    MaterialPipelineMeasurementResult MaterialPipelineMeasurement::Run(
        MaterialMeasurementMode mode, std::span<const CookedMaterialProgram> programs,
        MaterialPipelineMeasurementAdapter* adapter)
    {
        MaterialPipelineMeasurementResult result;
        if (mode == MaterialMeasurementMode::Disabled)
            return result;

        if (adapter == nullptr)
        {
            result.report.status = MaterialMeasurementStatus::Unavailable;
            result.report.unavailableReason = "no headless measurement adapter is available";
        }
        else
        {
            result.report = adapter->Measure(programs);
            if (result.report.status == MaterialMeasurementStatus::Disabled)
            {
                result.report.status = MaterialMeasurementStatus::Unavailable;
                result.report.unavailableReason = "measurement adapter returned no status";
            }
            else if (result.report.status == MaterialMeasurementStatus::Unavailable &&
                     result.report.unavailableReason.empty())
            {
                result.report.unavailableReason = "measurement adapter reported unavailable";
            }

            std::sort(result.report.statistics.begin(), result.report.statistics.end(),
                      [](const MaterialPipelineStatistic& left,
                         const MaterialPipelineStatistic& right) {
                          if (left.executionGroupID != right.executionGroupID)
                              return left.executionGroupID < right.executionGroupID;
                          if (left.groupLocalProgramID != right.groupLocalProgramID)
                              return left.groupLocalProgramID < right.groupLocalProgramID;
                          if (left.canonicalKey != right.canonicalKey)
                              return left.canonicalKey < right.canonicalKey;
                          return left.name < right.name;
                      });
            if (result.report.status == MaterialMeasurementStatus::Available)
                Analyze(result.report);
        }

        result.measurementRequirementMet =
            mode != MaterialMeasurementMode::Required ||
            result.report.status == MaterialMeasurementStatus::Available;
        return result;
    }

    void MaterialPipelineMeasurement::Analyze(MaterialPipelineMeasurementReport& report)
    {
        using GroupStatisticKey = std::pair<u16, std::string>;
        std::map<GroupStatisticKey, std::vector<f64>> valuesByGroupAndName;
        for (const MaterialPipelineStatistic& statistic : report.statistics)
        {
            if (statistic.format == "bool32" || !std::isfinite(statistic.value))
                continue;
            valuesByGroupAndName[{statistic.executionGroupID, statistic.name}].push_back(
                statistic.value);
        }

        for (auto& [key, values] : valuesByGroupAndName)
        {
            MaterialPipelineStatisticMedian& median = report.groupMedians.emplace_back();
            median.executionGroupID = key.first;
            median.name = key.second;
            median.value = Median(std::move(values));

            const f64 budget = StatisticBudget(median.name);
            if (budget > 0.0 && median.value > budget)
            {
                report.budgetWarnings.push_back(
                    {median.name, median.executionGroupID, median.value, budget});
            }
        }

        std::map<std::string, std::vector<const MaterialPipelineStatisticMedian*>> mediansByName;
        for (const MaterialPipelineStatisticMedian& median : report.groupMedians)
            mediansByName[median.name].push_back(&median);
        for (const auto& [name, medians] : mediansByName)
        {
            if (medians.size() < 3)
                continue;
            std::vector<f64> values;
            values.reserve(medians.size());
            for (const MaterialPipelineStatisticMedian* median : medians)
                values.push_back(median->value);
            const f64 crossGroupMedian = Median(std::move(values));
            if (crossGroupMedian <= 0.0)
                continue;
            for (const MaterialPipelineStatisticMedian* median : medians)
            {
                const f64 ratio = median->value / crossGroupMedian;
                if (ratio >= OUTLIER_RATIO || ratio <= 1.0 / OUTLIER_RATIO)
                {
                    report.outliers.push_back({name, median->executionGroupID, median->value,
                                               crossGroupMedian, ratio});
                }
            }
        }

        std::map<u16, std::map<std::string, f64>> groupValues;
        for (const MaterialPipelineStatisticMedian& median : report.groupMedians)
            groupValues[median.executionGroupID][median.name] = median.value;
        for (auto first = groupValues.begin(); first != groupValues.end(); ++first)
        {
            for (auto second = std::next(first); second != groupValues.end(); ++second)
            {
                if (first->second.size() != second->second.size() || first->second.empty())
                    continue;
                f64 maximumDifference = 0.0;
                bool comparable = true;
                for (const auto& [name, value] : first->second)
                {
                    const auto other = second->second.find(name);
                    if (other == second->second.end())
                    {
                        comparable = false;
                        break;
                    }
                    maximumDifference = std::max(
                        maximumDifference, RelativeDifference(value, other->second));
                }
                if (comparable && maximumDifference <= NEAR_IDENTICAL_RELATIVE_DIFFERENCE)
                {
                    report.nearIdenticalGroups.push_back(
                        {first->first, second->first, maximumDifference});
                }
            }
        }
    }
}
