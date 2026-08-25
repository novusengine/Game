#pragma once

#include <Base/Types.h>

#include <FileFormat/Novus/Model/ComplexModel.h>

#include <limits>
#include <memory>
#include <string>

namespace ModelLoading
{
    using ModelLoadRequestID = u64;
    constexpr ModelLoadRequestID INVALID_MODEL_LOAD_REQUEST_ID = 0;

    struct LoadedModelResult
    {
    public:
        u64 epoch = 0;
        u64 modelHash = std::numeric_limits<u64>().max();
        std::string debugName;
        std::unique_ptr<Model::ComplexModel> model;
        std::string error;

        explicit operator bool() const { return error.empty() && model != nullptr; }
    };
}
