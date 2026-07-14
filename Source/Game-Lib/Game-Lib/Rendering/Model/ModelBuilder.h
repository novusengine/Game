#pragma once

#include "ModelLoadTypes.h"

namespace ModelLoading
{
    // Pure CPU preparation. This function only reads model and writes job-owned output.
    ModelBuildResult BuildPreparedModel(const std::string& name, const Model::ComplexModel& model);
}
