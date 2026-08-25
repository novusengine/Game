#pragma once

#include <Base/Types.h>

#include <FileFormat/Novus/Model/Model.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace ModelLoading
{
    using ModelLoadRequestID = u64;
    constexpr ModelLoadRequestID INVALID_MODEL_LOAD_REQUEST_ID = 0;

    struct LoadedModelData
    {
        FileFormat::Model::ModelAsset asset;
        std::vector<u8> physicsData;
    };

    struct LoadedModelResult
    {
    public:
        u64 epoch = 0;
        u64 modelHash = std::numeric_limits<u64>().max();
        std::string debugName;
        std::unique_ptr<LoadedModelData> model;
        std::string error;

        explicit operator bool() const { return error.empty() && model != nullptr; }
    };
}
