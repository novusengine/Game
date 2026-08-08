#include "MaterialCooker.h"

namespace MaterialCooking
{
    MaterialCookResult MaterialCooker::Cook(const MaterialCookRequest& request)
    {
        MaterialCookResult result;
        result.plan = MaterialCookPlanBuilder::Build(
            request.sourcePrograms, request.assignments, request.requireCompleteSourceSemantics);
        result.functionalCookSucceeded = static_cast<bool>(result.plan);
        if (!result.functionalCookSucceeded)
            return result;

        result.measurement = MaterialPipelineMeasurement::Run(
            request.measurementMode, result.plan.programs, request.measurementAdapter);
        return result;
    }
}
